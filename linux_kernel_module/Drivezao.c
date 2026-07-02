// =============================================================================
// rover_kdriver.c - Driver Ring 0 para o Rover FPV
// Interceta eventos do Xbox via Input Subsystem e envia para a ESP32 via TCP
// =============================================================================
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rafael Gomes Oliveira Santos - IHS");
MODULE_DESCRIPTION("Driver Kernel Ring 0 para Rover FPV");

// Parâmetro dinâmico para passares o IP do Hotspot ao carregar o módulo
// Ex: sudo insmod rover_kdriver.ko esp_ip="192.168.100.34"
static char *esp_ip = "192.168.100.34";
module_param(esp_ip, charp, 0644);

static struct socket *sock = NULL;
static struct workqueue_struct *rover_wq;

// Estrutura para transportar o comando da Interrupção para a Workqueue
struct rover_work {
    struct work_struct work;
    char cmd;
};

// =============================================================================
// 1. O TRABALHADOR DE REDE (Roda em Process Context, pode bloquear)
// =============================================================================
static void enviar_tcp_worker(struct work_struct *work) {
    struct rover_work *rw = container_of(work, struct rover_work, work);
    struct kvec vec;
    struct msghdr msg;
    int ret;

    if (sock) {
        memset(&msg, 0, sizeof(msg));
        vec.iov_base = &rw->cmd;
        vec.iov_len = 1;

        // Envia o caractere via Kernel Sockets
        ret = kernel_sendmsg(sock, &msg, &vec, 1, 1);
        if (ret >= 0) {
            pr_info("[ROVER_KDRIVER] Comando '%c' disparado para %s:8080\n", rw->cmd, esp_ip);
        }
    }
    
    kfree(rw); // Liberta a memória alocada na interrupção
}

// =============================================================================
// 2. INTERCETADOR DE EVENTOS DE HARDWARE (HÍBRIDO: XBOX + TECLADO)
// =============================================================================
static void rover_event(struct input_handle *handle, unsigned int type, unsigned int code, int value) {
    struct rover_work *rw;
    char comando = 0;

    // Apenas nos importamos com o evento de "pressionar" o botão/tecla (value == 1)
    if (type == EV_KEY && value == 1) {
        
        // --- 1. MAPEAMENTO DO COMANDO XBOX ---
        // O driver xpad/bluetooth mapeia os botões para as macros BTN_*
        if (code == BTN_A || code == BTN_SOUTH) {
            comando = 'P'; // Botão A (ou Sul) -> PARAR
        } 
        else if (code == BTN_X || code == BTN_WEST) {
            comando = 'F'; // Botão X (ou Oeste) -> FRENTE
        }
        else if (code == BTN_Y || code == BTN_NORTH) {
            comando = 'T'; // Botão Y (ou Norte) -> TRÁS
        }
        
        // --- 2. MAPEAMENTO DO TECLADO DO PC (Backup de Segurança) ---
        // Permite controlar o robô usando as setas do teclado
        else if (code == KEY_UP) {
            comando = 'F'; // Seta Cima -> FRENTE
        }
        else if (code == KEY_DOWN) {
            comando = 'T'; // Seta Baixo -> TRÁS
        }
        else if (code == KEY_SPACE) {
            comando = 'P'; // Barra de Espaço -> PARAR
        }
    }

        // Se detetámos um comando válido (seja do Xbox ou do Teclado)
        if (comando != 0) {
            // Aloca memória atômica (contexto de interrupção não pode dormir)
            rw = kmalloc(sizeof(*rw), GFP_ATOMIC);
            if (rw) {
                rw->cmd = comando;
                INIT_WORK(&rw->work, enviar_tcp_worker);
                queue_work(rover_wq, &rw->work); // Despacha para enviar via TCP
            }
        }

// =============================================================================
// 3. BOILERPLATE DO INPUT SUBSYSTEM (Para detetar qualquer comando)
// =============================================================================
static int rover_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id) {
    struct input_handle *handle;
    int error;

    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle) return -ENOMEM;

    handle->dev = dev;
    handle->handler = handler;
    handle->name = "rover_handler";

    error = input_register_handle(handle);
    if (error) goto err_free_handle;

    error = input_open_device(handle);
    if (error) goto err_unregister_handle;

    pr_info("[ROVER_KDRIVER] Anexado ao dispositivo: %s\n", dev->name);
    return 0;

err_unregister_handle:
    input_unregister_handle(handle);
err_free_handle:
    kfree(handle);
    return error;
}

static void rover_disconnect(struct input_handle *handle) {
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

// Deteta QUALQUER dispositivo de entrada (Filtramos por botões no rover_event)
static const struct input_device_id rover_ids[] = {
    { .driver_info = 1 }, 
    { },
};
MODULE_DEVICE_TABLE(input, rover_ids);

static struct input_handler rover_input_handler = {
    .event = rover_event,
    .connect = rover_connect,
    .disconnect = rover_disconnect,
    .name = "rover_input_handler",
    .id_table = rover_ids,
};

// =============================================================================
// 4. INICIALIZAÇÃO DO MÓDULO DO KERNEL
// =============================================================================
static int __init rover_init(void) {
    struct sockaddr_in srv_addr;
    int ret;

    pr_info("[ROVER_KDRIVER] Carregando Módulo. Alvo IP: %s\n", esp_ip);

    // Cria a Workqueue para a rede
    rover_wq = create_singlethread_workqueue("rover_wq");
    if (!rover_wq) return -ENOMEM;

    // Inicia o Kernel Socket TCP
    ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (ret < 0) {
        pr_err("[ROVER_KDRIVER] Erro ao criar socket: %d\n", ret);
        destroy_workqueue(rover_wq);
        return ret;
    }

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(8080);
    srv_addr.sin_addr.s_addr = in_aton(esp_ip);

    // Conecta à ESP32
    ret = sock->ops->connect(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr), 0);
    if (ret < 0) {
        pr_err("[ROVER_KDRIVER] Falha ao conectar à ESP32. Confirme o IP!\n");
        // Continuamos mesmo assim, pode ser que a ESP32 ligue depois
    } else {
        pr_info("[ROVER_KDRIVER] TCP Conectado à ESP32 no Ring 0!\n");
    }

    // Regista o hook no Subsistema de Entrada
    return input_register_handler(&rover_input_handler);
}

static void __exit rover_exit(void) {
    input_unregister_handler(&rover_input_handler);
    
    if (sock) {
        sock_release(sock);
    }
    
    flush_workqueue(rover_wq);
    destroy_workqueue(rover_wq);
    
    pr_info("[ROVER_KDRIVER] Módulo descarregado.\n");
}

module_init(rover_init);
module_exit(rover_exit);