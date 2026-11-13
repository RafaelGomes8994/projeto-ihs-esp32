// =============================================================================
// Drivezao.c - Driver de entrada para o Rover FPV
// Encaminha comandos de um Xbox ou teclado para a ESP32 por TCP.
// =============================================================================
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/socket.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rafael Gomes Oliveira Santos - IHS");
MODULE_DESCRIPTION("Driver de controle TCP para Rover FPV");

#define DRIVEZAO_DEFAULT_IP            "192.168.100.65"
#define DRIVEZAO_DEFAULT_PORT          8080
#define DRIVEZAO_DEFAULT_HEARTBEAT_MS  100
#define DRIVEZAO_MIN_HEARTBEAT_MS      20
#define DRIVEZAO_MAX_HEARTBEAT_MS      400
#define DRIVEZAO_RECONNECT_MS          1000

#define DRIVEZAO_CMD_FRENTE 'F'
#define DRIVEZAO_CMD_TRAS   'T'
#define DRIVEZAO_CMD_PARAR  'P'

#define DRIVEZAO_HELD_FRENTE BIT(0)
#define DRIVEZAO_HELD_TRAS   BIT(1)

/* Parametros de carga: esp_ip="192.168.100.65" esp_port=8080 */
static char *esp_ip = DRIVEZAO_DEFAULT_IP;
static ushort esp_port = DRIVEZAO_DEFAULT_PORT;
static uint heartbeat_ms = DRIVEZAO_DEFAULT_HEARTBEAT_MS;

module_param(esp_ip, charp, 0444);
MODULE_PARM_DESC(esp_ip, "Endereco IPv4 da ESP32");
module_param(esp_port, ushort, 0444);
MODULE_PARM_DESC(esp_port, "Porta TCP da ESP32");
module_param(heartbeat_ms, uint, 0444);
MODULE_PARM_DESC(heartbeat_ms, "Intervalo entre comandos mantidos, em ms (20-400)");

struct drivezao_handle {
    struct input_handle handle;
    struct list_head node;
    unsigned long held;
    bool active;
};

static struct socket *esp_sock;
static struct workqueue_struct *drivezao_wq;
static struct delayed_work command_work;

/*
 * O callback de input pode rodar em contexto atomico. Ele apenas atualiza este
 * estado e agenda command_work; toda operacao de rede ocorre na workqueue.
 */
static DEFINE_SPINLOCK(state_lock);
static LIST_HEAD(drivezao_handles);
static char requested_command = DRIVEZAO_CMD_PARAR;
static unsigned int frente_holders;
static unsigned int tras_holders;
static bool shutting_down;
static char last_logged_command;

static bool drivezao_is_movement(char command)
{
    return command == DRIVEZAO_CMD_FRENTE || command == DRIVEZAO_CMD_TRAS;
}

static void drivezao_schedule_locked(unsigned long delay)
{
    if (!shutting_down && drivezao_wq)
        mod_delayed_work(drivezao_wq, &command_work, delay);
}

static int drivezao_build_address(struct sockaddr_in *address)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(esp_port);

    if (!in4_pton(esp_ip, -1, (u8 *)&address->sin_addr.s_addr, '\0', NULL)) {
        pr_err("[DRIVEZAO] esp_ip invalido: %s\n", esp_ip);
        return -EINVAL;
    }

    return 0;
}

/* Called only by command_work, or after command_work was cancelled on exit. */
static void drivezao_close_socket(void)
{
    if (esp_sock) {
        sock_release(esp_sock);
        esp_sock = NULL;
    }

    last_logged_command = '\0';
}

static int drivezao_connect_tcp(void)
{
    struct sockaddr_in address;
    struct socket *new_sock;
    int ret;

    if (esp_sock)
        return 0;

    ret = drivezao_build_address(&address);
    if (ret)
        return ret;

    ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &new_sock);
    if (ret < 0) {
        pr_err_ratelimited("[DRIVEZAO] Nao foi possivel criar socket TCP: %d\n",
                           ret);
        return ret;
    }

    ret = kernel_connect(new_sock, (struct sockaddr *)&address,
                         sizeof(address), 0);
    if (ret < 0) {
        pr_warn_ratelimited("[DRIVEZAO] ESP32 indisponivel em %s:%u: %d\n",
                            esp_ip, esp_port, ret);
        sock_release(new_sock);
        return ret;
    }

    esp_sock = new_sock;
    pr_info("[DRIVEZAO] TCP conectado a %s:%u\n", esp_ip, esp_port);
    return 0;
}

static int drivezao_send_connected(char command)
{
    struct kvec vec = {
        .iov_base = &command,
        .iov_len = 1,
    };
    struct msghdr msg = { };
    int ret;

    if (!esp_sock)
        return -ENOTCONN;

    ret = kernel_sendmsg(esp_sock, &msg, &vec, 1, 1);
    if (ret == 1) {
        if (command != last_logged_command) {
            pr_info("[DRIVEZAO] Comando '%c' enviado para %s:%u\n",
                    command, esp_ip, esp_port);
            last_logged_command = command;
        }
        return 0;
    }

    if (ret >= 0)
        ret = -EIO;

    pr_warn_ratelimited("[DRIVEZAO] Falha ao enviar comando '%c': %d\n",
                        command, ret);
    return ret;
}

static int drivezao_send_command(char command)
{
    int ret;

    ret = drivezao_connect_tcp();
    if (ret)
        return ret;

    ret = drivezao_send_connected(command);
    if (ret)
        drivezao_close_socket();

    return ret;
}

/* Do not open a new connection while the module is being removed. */
static void drivezao_send_stop_if_connected(void)
{
    if (esp_sock && drivezao_send_connected(DRIVEZAO_CMD_PARAR))
        drivezao_close_socket();
}

static void drivezao_command_worker(struct work_struct *work)
{
    char command;
    unsigned long flags;
    unsigned long next_delay;
    int ret;

    spin_lock_irqsave(&state_lock, flags);
    if (shutting_down) {
        spin_unlock_irqrestore(&state_lock, flags);
        return;
    }
    command = requested_command;
    spin_unlock_irqrestore(&state_lock, flags);

    ret = drivezao_send_command(command);

    spin_lock_irqsave(&state_lock, flags);
    if (!shutting_down) {
        /* A state change during TCP I/O must be transmitted without waiting. */
        if (requested_command != command) {
            drivezao_schedule_locked(0);
        } else if (drivezao_is_movement(command)) {
            next_delay = ret ? msecs_to_jiffies(DRIVEZAO_RECONNECT_MS) :
                               msecs_to_jiffies(heartbeat_ms);
            drivezao_schedule_locked(next_delay);
        }
    }
    spin_unlock_irqrestore(&state_lock, flags);
}

static void drivezao_clear_held_locked(void)
{
    struct drivezao_handle *dh;

    list_for_each_entry(dh, &drivezao_handles, node)
        dh->held = 0;

    frente_holders = 0;
    tras_holders = 0;
    requested_command = DRIVEZAO_CMD_PARAR;
}

static void drivezao_request_stop(struct drivezao_handle *dh)
{
    unsigned long flags;

    spin_lock_irqsave(&state_lock, flags);
    if (!shutting_down && dh->active) {
        /* A/B or Space is an explicit emergency stop. */
        drivezao_clear_held_locked();
        drivezao_schedule_locked(0);
    }
    spin_unlock_irqrestore(&state_lock, flags);
}

static void drivezao_update_direction(struct drivezao_handle *dh,
                                      char command, bool pressed)
{
    unsigned long bit;
    unsigned int *holders;
    unsigned long flags;
    bool send_now = false;

    if (command == DRIVEZAO_CMD_FRENTE) {
        bit = DRIVEZAO_HELD_FRENTE;
        holders = &frente_holders;
    } else {
        bit = DRIVEZAO_HELD_TRAS;
        holders = &tras_holders;
    }

    spin_lock_irqsave(&state_lock, flags);
    if (shutting_down || !dh->active)
        goto unlock;

    if (pressed) {
        /* Ignore EV_KEY autorepeat; a held command is refreshed by heartbeat. */
        if (dh->held & bit)
            goto unlock;

        dh->held |= bit;
        ++*holders;
        requested_command = command;
        send_now = true;
    } else {
        if (!(dh->held & bit))
            goto unlock;

        dh->held &= ~bit;
        if (*holders)
            --*holders;

        if (requested_command == command && !*holders) {
            if (command == DRIVEZAO_CMD_FRENTE && tras_holders)
                requested_command = DRIVEZAO_CMD_TRAS;
            else if (command == DRIVEZAO_CMD_TRAS && frente_holders)
                requested_command = DRIVEZAO_CMD_FRENTE;
            else
                requested_command = DRIVEZAO_CMD_PARAR;

            /* Release is not delayed until the next heartbeat. */
            send_now = true;
        }
    }

    if (send_now)
        drivezao_schedule_locked(0);

unlock:
    spin_unlock_irqrestore(&state_lock, flags);
}

static void drivezao_detach_handle(struct drivezao_handle *dh)
{
    unsigned long flags;
    bool send_now = false;

    spin_lock_irqsave(&state_lock, flags);
    if (!dh->active)
        goto unlock;

    dh->active = false;

    if (dh->held & DRIVEZAO_HELD_FRENTE && frente_holders)
        --frente_holders;
    if (dh->held & DRIVEZAO_HELD_TRAS && tras_holders)
        --tras_holders;
    dh->held = 0;

    list_del_init(&dh->node);

    if (requested_command == DRIVEZAO_CMD_FRENTE && !frente_holders) {
        requested_command = tras_holders ? DRIVEZAO_CMD_TRAS :
                                           DRIVEZAO_CMD_PARAR;
        send_now = true;
    } else if (requested_command == DRIVEZAO_CMD_TRAS && !tras_holders) {
        requested_command = frente_holders ? DRIVEZAO_CMD_FRENTE :
                                             DRIVEZAO_CMD_PARAR;
        send_now = true;
    }

    if (!shutting_down && send_now)
        drivezao_schedule_locked(0);

unlock:
    spin_unlock_irqrestore(&state_lock, flags);
}

static bool drivezao_supported_device(const struct input_dev *dev)
{
    bool xbox_like;
    bool keyboard;

    xbox_like = test_bit(BTN_A, dev->keybit) &&
                test_bit(BTN_X, dev->keybit) &&
                test_bit(BTN_Y, dev->keybit);
    keyboard = test_bit(KEY_UP, dev->keybit) &&
               test_bit(KEY_DOWN, dev->keybit) &&
               test_bit(KEY_SPACE, dev->keybit);

    return xbox_like || keyboard;
}

static void drivezao_event(struct input_handle *handle,
                           unsigned int type, unsigned int code, int value)
{
    struct drivezao_handle *dh =
        container_of(handle, struct drivezao_handle, handle);
    char command;

    if (type != EV_KEY || (value != 0 && value != 1))
        return;

    /*
     * Xbox: X -> FRENTE, Y -> TRAS, A/B -> PARAR.
     * Teclado: seta para cima -> FRENTE, para baixo -> TRAS, espaco -> PARAR.
     */
    switch (code) {
    case BTN_X:
    case KEY_UP:
        command = DRIVEZAO_CMD_FRENTE;
        break;
    case BTN_Y:
    case KEY_DOWN:
        command = DRIVEZAO_CMD_TRAS;
        break;
    case BTN_A:
    case BTN_B:
    case KEY_SPACE:
        command = DRIVEZAO_CMD_PARAR;
        break;
    default:
        return;
    }

    if (command == DRIVEZAO_CMD_PARAR) {
        if (value == 1)
            drivezao_request_stop(dh);
        return;
    }

    drivezao_update_direction(dh, command, value == 1);
}

static int drivezao_connect_input(struct input_handler *handler,
                                  struct input_dev *dev,
                                  const struct input_device_id *id)
{
    struct drivezao_handle *dh;
    unsigned long flags;
    int ret;

    if (!drivezao_supported_device(dev))
        return -ENODEV;

    dh = kzalloc(sizeof(*dh), GFP_KERNEL);
    if (!dh)
        return -ENOMEM;

    INIT_LIST_HEAD(&dh->node);
    dh->handle.dev = dev;
    dh->handle.handler = handler;
    dh->handle.name = "drivezao_handler";

    ret = input_register_handle(&dh->handle);
    if (ret)
        goto err_free_handle;

    spin_lock_irqsave(&state_lock, flags);
    if (shutting_down) {
        ret = -ESHUTDOWN;
    } else {
        dh->active = true;
        list_add_tail(&dh->node, &drivezao_handles);
        ret = 0;
    }
    spin_unlock_irqrestore(&state_lock, flags);
    if (ret)
        goto err_unregister_handle;

    ret = input_open_device(&dh->handle);
    if (ret)
        goto err_detach_handle;

    pr_info("[DRIVEZAO] Entrada anexada: %s\n",
            dev->name ? dev->name : "sem nome");
    return 0;

err_detach_handle:
    drivezao_detach_handle(dh);
err_unregister_handle:
    input_unregister_handle(&dh->handle);
err_free_handle:
    kfree(dh);
    return ret;
}

static void drivezao_disconnect_input(struct input_handle *handle)
{
    struct drivezao_handle *dh =
        container_of(handle, struct drivezao_handle, handle);

    drivezao_detach_handle(dh);
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(dh);
}

/* Match only keyboards with the chosen keys or gamepads with the Xbox map. */
static const struct input_device_id drivezao_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT |
                 INPUT_DEVICE_ID_MATCH_KEYBIT,
        .evbit = { BIT_MASK(EV_KEY) },
        .keybit = {
            [BIT_WORD(BTN_A)] = BIT_MASK(BTN_A) |
                                BIT_MASK(BTN_X) |
                                BIT_MASK(BTN_Y),
        },
    },
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT |
                 INPUT_DEVICE_ID_MATCH_KEYBIT,
        .evbit = { BIT_MASK(EV_KEY) },
        .keybit = {
            [BIT_WORD(KEY_SPACE)] = BIT_MASK(KEY_SPACE),
            [BIT_WORD(KEY_UP)] = BIT_MASK(KEY_UP) | BIT_MASK(KEY_DOWN),
        },
    },
    { },
};
MODULE_DEVICE_TABLE(input, drivezao_ids);

static struct input_handler drivezao_input_handler = {
    .event = drivezao_event,
    .connect = drivezao_connect_input,
    .disconnect = drivezao_disconnect_input,
    .name = "drivezao_input_handler",
    .id_table = drivezao_ids,
};

static int __init drivezao_init(void)
{
    struct sockaddr_in address;
    unsigned long flags;
    int ret;

    if (!esp_port) {
        pr_err("[DRIVEZAO] esp_port deve estar entre 1 e 65535\n");
        return -EINVAL;
    }

    ret = drivezao_build_address(&address);
    if (ret)
        return ret;

    if (heartbeat_ms < DRIVEZAO_MIN_HEARTBEAT_MS ||
        heartbeat_ms > DRIVEZAO_MAX_HEARTBEAT_MS) {
        pr_warn("[DRIVEZAO] heartbeat_ms=%u invalido; usando %u ms\n",
                heartbeat_ms, DRIVEZAO_DEFAULT_HEARTBEAT_MS);
        heartbeat_ms = DRIVEZAO_DEFAULT_HEARTBEAT_MS;
    }

    INIT_DELAYED_WORK(&command_work, drivezao_command_worker);
    drivezao_wq = alloc_ordered_workqueue("drivezao_wq", WQ_MEM_RECLAIM);
    if (!drivezao_wq)
        return -ENOMEM;

    spin_lock_irqsave(&state_lock, flags);
    shutting_down = false;
    drivezao_clear_held_locked();
    spin_unlock_irqrestore(&state_lock, flags);

    ret = input_register_handler(&drivezao_input_handler);
    if (ret) {
        pr_err("[DRIVEZAO] Falha ao registrar handler de input: %d\n", ret);
        cancel_delayed_work_sync(&command_work);
        destroy_workqueue(drivezao_wq);
        drivezao_wq = NULL;
        return ret;
    }

    pr_info("[DRIVEZAO] Carregado. Alvo %s:%u; heartbeat %u ms.\n",
            esp_ip, esp_port, heartbeat_ms);
    pr_info("[DRIVEZAO] TCP sera conectado sob demanda no primeiro comando.\n");
    return 0;
}

static void __exit drivezao_exit(void)
{
    unsigned long flags;

    /* Block new schedules before unregistering input and draining the worker. */
    spin_lock_irqsave(&state_lock, flags);
    shutting_down = true;
    drivezao_clear_held_locked();
    spin_unlock_irqrestore(&state_lock, flags);

    input_unregister_handler(&drivezao_input_handler);

    /* No worker may touch esp_sock after this point. */
    cancel_delayed_work_sync(&command_work);
    drivezao_send_stop_if_connected();
    drivezao_close_socket();

    if (drivezao_wq) {
        destroy_workqueue(drivezao_wq);
        drivezao_wq = NULL;
    }

    pr_info("[DRIVEZAO] Modulo descarregado.\n");
}

module_init(drivezao_init);
module_exit(drivezao_exit);
