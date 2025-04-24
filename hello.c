#include <stdint.h>
#define UART0_BASE 0x10000000

// Use a datasheet for a 16550 UART used with Qemu Virt
// For example: https://www.ti.com/lit/ds/symlink/tl16c550d.pdf
#define REG(base, offset) ((*((volatile unsigned char *)(base + offset))))
#define UART0_DR    REG(UART0_BASE, 0x00)
#define UART0_FCR   REG(UART0_BASE, 0x02)
#define UART0_LSR   REG(UART0_BASE, 0x05)																			
#define UARTFCR_FFENA 0x01         // UART FIFO Control Register enable bit
#define UARTLSR_THRE 0x20          // UART Line Status Register Transmit Hold Register Empty bit
#define UART0_FF_THR_EMPTY (UART0_LSR & UARTLSR_THRE)

// PMP Configs
#define PMP_R 0x01
#define PMP_W 0x02
#define PMP_X 0x04
#define PMP_NAPOT 0x18
static inline void write_csr(const char* csr_name, uintptr_t value);
volatile uint32_t protected_buffer[4] __attribute__((aligned(16))) = {1, 2, 3, 4};

// Uart Driver Functions
void uart_putc(char c) {
  while (!UART0_FF_THR_EMPTY);            // Wait until the FIFO holding register is empty
  UART0_DR = c;                           // Write character to transmitter register
}

void uart_puts(const char *str) {
  while (*str) {                          // Loop until value at string pointer is zero
    uart_putc(*str++);                    // Write the character and increment pointer
  }
}

// PMP Interface Functions
void setup_pmp_region(uintptr_t addr, uintptr_t size) {
    // NAPOT encoding: region size must be power of 2 and >= 8 bytes
    uintptr_t pmpaddr = ((addr >> 2) | ((size / 2 - 1) >> 3));

    // Configure PMP0 to no-access region (no R/W/X)
    asm volatile (
        "li t0, 0\n"
        "csrw pmpcfg0, t0\n"
    );

    // Set PMP0 address
    asm volatile (
        "csrw pmpaddr0, %0\n" :: "r"(pmpaddr)
    );

    // Configure PMP0 as NAPOT with no permissions
    uint8_t cfg = PMP_NAPOT; // no R/W/X
    asm volatile (
        "csrw pmpcfg0, %0\n" :: "r"(cfg)
    );
}

void enter_user_mode(void (*user_fn)()) {
    uintptr_t user_stack = 0x81000000; // pick an address you didn't PMP-protect
    uintptr_t mstatus;
    asm volatile ("csrr %0, mstatus" : "=r"(mstatus));

    // Clear MPP bits [12:11], set to 00 (U-mode)
    mstatus = (mstatus & ~0x1800);

    asm volatile (
        "csrw mepc, %0\n"
        "csrw mstatus, %1\n"
        "mv sp, %2\n"
        "mret\n"
        :: "r"(user_fn), "r"(mstatus), "r"(user_stack)
    );
}

void user_code() {
    volatile uint32_t value = protected_buffer[0];  // Should trap
    uart_puts("SURVIVED :( \n");
    while (1); // If we survive, loop here
}

void trap_handler() {
    uint32_t mcause, mepc;
    asm volatile("csrr %0, mcause" : "=r"(mcause));
    asm volatile("csrr %0, mepc" : "=r"(mepc));
    // You can inspect mcause for fault code: 0x0000000d = load access fault
    
    uart_puts("TRAP CAUGHT - VIOLATION HANDLED\n");
    while (1);
}

void init_trap() {
    asm volatile("csrw mtvec, %0" :: "r"(trap_handler));
}

// Main
void main() {
  UART0_FCR = UARTFCR_FFENA;              // Set the FIFO for polled operation
  uart_puts("Hello World!\n"); 		  // Write the string to the UART
  
  // PMP Executions
  uart_puts("Init Trap\n");
  init_trap(); 				// Install Trap Handler
  uart_puts("PMP Setup\n");
  setup_pmp_region((uintptr_t)protected_buffer, sizeof(protected_buffer)); // Set up PMP
  uart_puts("Dropping to User Mode\n");
  enter_user_mode(user_code);
  
  uart_puts("This should never run\n"); 
  while (1);                              // Loop forever to prevent program from ending
}

/*
Read about priv spec & actual RISCV-SBI & Native Client Trampolines

Make an SBI for user mode to request M-Mode operations
SBI Handler to apply memory permissions to pmp regions

Buffer User Mode
FFI - M mode --> make buffer read only before dropping back to user 
passing pointer to a function which reads, prints to uart
On function end, 


How to preven functions from making another SBI call to remove permission restrictions
Restrict inner function from making an SBI call

SBI Call uses PC to determine if a restricted function attempted call
*/