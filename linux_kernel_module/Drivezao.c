// =============================================================================
// rover_kdriver.c — Linux Kernel Module para Controle TCP do Rover FPV
// =============================================================================
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/inet.h>
#include <net/sock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Engenharia de Sistemas - IHS UFS");
MODULE_DESCRIPTION("Driver de Kernel para interceptacao do Xbox e envio TCP");
MODULE_VERSION("1.0");

// --- CONFIGURAÇÃO DA ESP32 ---
#define ESP32_IP "192.168.100.34" // IP real da tua ESP32
#define ESP32_PORT 8080

static struct socket *rover_sock = NULL;
static struct workqueue_struct *rover_wq;

// Estrutura para a fila de trabalho (Deferred Work)
struct rover_work_data {
    struct work_struct work;
    char comando;
};

// =============================================================================
// WORKQUEUE: Envio de pacotes TCP fora do contexto de interrupção
// =============================================================================
static void rover_send_work_handler(struct work_struct *work) {
    struct rover_work_data *data = container_of(work, struct rover_work_data, work);
    struct kvec iov;
    struct msghdr msg;
    int ret;

    if (!rover_sock) {
        kfree(data);
        return;
    }

    memset(&msg, 0, sizeof(msg));
    iov.iov_base = &data->comando;
    iov.iov_len  = 1;

    // Função de núcleo para enviar dados por socket
    ret = kernel_sendmsg(rover_sock, &msg, &iov, 1, 1);
    if (ret < 0) {
        pr_err("[ROVER_KDRIVER] Falha ao enviar TCP. Codigo: %d\n", ret);
    } else {
        pr_info("[ROVER_KDRIVER] TCP Enviado com sucesso: %c\n", data->comando);
    }

    kfree(data);
}

// =============================================================================
// INTERCEPTAÇÃO: Subsistema de Input do Linux
// =============================================================================
static void rover_input_event(struct input_handle *handle, unsigned int type, unsigned int code, int value) {
    // Filtramos apenas pressionamentos de botões (EV_KEY)
    if (type == EV_KEY) {
        char cmd = 0;
        
        // No Linux, o Xbox mapeia botões para BTN_X, BTN_Y, BTN_B, etc.
        // value == 1 significa Pressionado. value == 0 significa Solto.
        if (code == BTN_X && value == 1) {
            cmd = 'F'; // Frente
        } else if (code == BTN_Y && value == 1) {
            cmd = 'T'; // Trás
        } else if (code == BTN_B && value == 1) {
            cmd = 'P'; // Parar (Freio)
        } else if (value == 0 && (code == BTN_X || code == BTN_Y)) {
            // Se soltou os de movimento, garante que para
            cmd = 'P'; 
        }

        if (cmd != 0) {
            struct rover_work_data *data = kmalloc(sizeof(*data), GFP_ATOMIC);
            if (data) {
                data->comando = cmd;
                INIT_WORK(&data->work, rover_send_work_handler);
                queue_work(rover_wq, &data->work);
            }
        }
    }
}

// Funções obrigatórias para o input_handler acoplar ao dispositivo
static bool rover_input_match(struct input_handler *handler, struct input_dev *dev) {
    // Aceita qualquer dispositivo que suporte a tecla BTN_X (Gamepad genérico)
    return test_bit(BTN_X, dev->keybit); 
}

static int rover_input_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id) {
    struct input_handle *handle;
    int error;

    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle) return -ENOMEM;

    handle->dev = dev;
    handle->handler = handler;
    handle->name = "rover_handle";

    error = input_register_handle(handle);
    if (error) goto err_free;

    error = input_open_device(handle);
    if (error) goto err_unregister;

    pr_info("[ROVER_KDRIVER] Gamepad conectado: %s\n", dev->name);
    return 0;

err_unregister:
    input_unregister_handle(handle);
err_free:
    kfree(handle);
    return error;
}

static void rover_input_disconnect(struct input_handle *handle) {
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static const struct input_device_id rover_ids[] = {
    { .driver_info = 1 }, // Captura todos
    { },
};
MODULE_DEVICE_TABLE(input, rover_ids);

static struct input_handler rover_handler = {
    .event      = rover_input_event,
    .match      = rover_input_match,
    .connect    = rover_input_connect,
    .disconnect = rover_input_disconnect,
    .name       = "rover_xbox_handler",
    .id_table   = rover_ids,
};

// =============================================================================
// INIT & EXIT DO KERNEL
// =============================================================================
static int __init rover_kdriver_init(void) {
    struct sockaddr_in dest_addr;
    int ret;

    pr_info("[ROVER_KDRIVER] Carregando modulo no Ring 0...\n");

    rover_wq = create_singlethread_workqueue("rover_wq");

    // 1. Abre Socket no Kernel
    ret = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &rover_sock);
    if (ret < 0) return ret;

    // 2. Conecta à ESP32
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(ESP32_PORT);
    dest_addr.sin_addr.s_addr = in_aton(ESP32_IP);

    ret = rover_sock->ops->connect(rover_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr), O_NONBLOCK);
    if (ret < 0 && ret != -EINPROGRESS) {
        pr_err("[ROVER_KDRIVER] Falha ao conectar TCP na ESP32.\n");
        sock_release(rover_sock);
        return ret;
    }

    // 3. Regista o Input Handler
    input_register_handler(&rover_handler);
    pr_info("[ROVER_KDRIVER] Modulo ativo! Escutando Gamepad...\n");
    return 0;
}

static void __exit rover_kdriver_exit(void) {
    input_unregister_handler(&rover_handler);
    flush_workqueue(rover_wq);
    destroy_workqueue(rover_wq);
    if (rover_sock) sock_release(rover_sock);
    pr_info("[ROVER_KDRIVER] Modulo descarregado e portas fechadas.\n");
}

module_init(rover_kdriver_init);
module_exit(rover_kdriver_exit);