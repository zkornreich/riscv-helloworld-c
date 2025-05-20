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
#define PMP_LOCK 0x80
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

void uart_put_hex_nibble(uint8_t nibble) {
  if (nibble < 10)
      uart_putc('0' + nibble);
  else
      uart_putc('A' + (nibble - 10));
}

void uart_put_hex32(uint32_t value) {
  for (int i = 7; i >= 0; i--) {
      uint8_t nibble = (value >> (i * 4)) & 0xF;
      uart_put_hex_nibble(nibble);
  }
}

void print_hex(const char *label, uint32_t value) {
  uart_puts(label);
  uart_puts("0x");
  uart_put_hex32(value);
  uart_putc('\n');
}

// PMP Interface Functions
void setup_pmp_region(uintptr_t addr, uintptr_t size) {
    uintptr_t pmpaddr = ((addr >> 2) | ((size / 2 - 1) >> 3));

    // Set PMP0 address
    asm volatile (
        "csrw pmpaddr0, %0\n" :: "r"(pmpaddr)
    );

    // Configure PMP0 as NAPOT with no R/W/X
    //uint8_t cfg = PMP_NAPOT | PMP_LOCK;
    uint8_t cfg = PMP_NAPOT;
    asm volatile (
        "csrw pmpcfg0, %0\n" :: "r"(cfg)
    );
}

// // PMP Interface Functions
void setup_user_region(uintptr_t addr, uintptr_t size) {
  uintptr_t pmpaddr = ((addr >> 2) | ((size / 2 - 1) >> 3));
  // Set PMP0 address
  asm volatile (
      "csrw pmpaddr1, %0\n" :: "r"(pmpaddr)
  );

  // Configure PMP0 as NAPOT with R/W/X
  uint8_t cfg = PMP_R | PMP_W | PMP_X | PMP_NAPOT;   
  print_hex("cfg: ", cfg);
  asm volatile (
      "csrw pmpcfg1, %0\n" :: "r"(cfg)
  );
}

void enter_user_mode(void (*user_fn)()) {
    uintptr_t user_stack = 0x81020000; 
    uintptr_t mstatus = 1;
    uint32_t mepc;
    asm volatile ("csrr %0, mstatus" : "=r"(mstatus));
    // print_hex("mstatus before clear: ", mstatus);

    // Clear MPP bits [12:11], set to 00 (U-mode)
    mstatus = ((mstatus & ~0x1800) | (1<<7));

    uart_puts("Dropping to User Mode\n");
    // print_hex("user_fn address: ", (uintptr_t)user_fn);
    // print_hex("mstatus post clear: ", mstatus);

    // asm volatile (
    //     "csrw mepc, %0\n"
    //     "csrw mstatus, %1\n"
    //     "mv sp, %2\n"
    //     "mret\n"
    //     :: "r"(user_fn), "r"(mstatus), "r"(user_stack)
    // );

    // Set mepc to user function
    asm volatile("csrw mepc, %0" :: "r"((uint32_t) user_fn));
    // asm volatile("li t0, 0x800002F4; csrw mepc, t0");

    // Set mstatus to drop to U-mode
    asm volatile("csrw mstatus, %0" :: "r"(mstatus));
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    print_hex("mstatus after write: ", mstatus);

    // Set user stack pointer
    asm volatile("mv sp, %0" :: "r"(user_stack));
    asm volatile("csrr %0, mepc"   : "=r"(mepc));
    print_hex("MEPC: ", mepc);

    // Enter user mode
    // setup_user_region(user_fn, 0x20);
    asm volatile("mret");
    asm volatile ("csrr %0, mstatus" : "=r"(mstatus));
    print_hex("mstatus after mret: ", mstatus);
}

void user_code() {
  // volatile uint32_t value = protected_buffer[0];  // Should trap
  uart_puts("SURVIVED :( \n");
  while (1);
}

void trap_handler() {
  uint32_t mcause, mepc, mtval, mstatus;
  asm volatile("csrr %0, mcause" : "=r"(mcause));
  asm volatile("csrr %0, mepc"   : "=r"(mepc));
  asm volatile("csrr %0, mtval"  : "=r"(mtval));
  asm volatile("csrr %0, mstatus"  : "=r"(mstatus));

    
  uart_puts("TRAP\n");
  print_hex("mcause: ", mcause);
  print_hex("mepc:   ", mepc);
  print_hex("mtval:  ", mtval);
  print_hex("mstatus:  ", mstatus);
  while (1);
}

void init_trap() {
    asm volatile("csrw mtvec, %0" :: "r"(trap_handler));
}

void main() {
  // Set the FIFO for polled operation
  UART0_FCR = UARTFCR_FFENA;
  uart_puts("Prog Start!\n");

  
  asm volatile ("csrr t1, sstatus");

  print_hex("protected buffer start: ", (uint32_t) &protected_buffer[0]);
  print_hex("protected buffer end: ", (uint32_t) &protected_buffer[5]);

  // PMP Executions
  // Enable User Mode Code
  // setup_user_region((uintptr_t) user_code, 32);
  // uart_puts("From Main - Enter User Mode!\n");
  print_hex("enter_user_mode: ", (uintptr_t) enter_user_mode);
  setup_user_region((uintptr_t) enter_user_mode, 512);
  init_trap();
  setup_pmp_region((uintptr_t)protected_buffer, sizeof(protected_buffer)); // Set up PMP
  uart_puts("From Main - Enter User Mode!\n");
  enter_user_mode(user_code);

  // uint32_t illegal = protected_buffer[0];
  
  uart_puts("This should never run\n"); 
  while (1);
}

/*
Read about priv spec & actual RISCV-SBI & Native Client Trampolines

Make an SBI for user mode to request M-Mode PMP operations
SBI Handler to apply memory permissions to pmp regions

Buffer mode in User Mode
FFI - M mode --> make buffer read only before dropping to untrusted function, 
passing a pointer to the NOW READ ONLY memory.
Function 2 (untrusted) reads buffer prints to uart.
Function 2 ends --> Set buffer to read/write. Modify buffer contents, print new content
then protect again and pass to untrusted function 3. This function should attempt an
illegal write, which should activate the trap.

NEXT WEEK: 
How to prevent functions from making another SBI call to remove permission restrictions
Restrict inner function from making an SBI call

SBI Call uses PC to determine if a restricted function attempted call
*/