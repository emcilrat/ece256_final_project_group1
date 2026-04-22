#include <stdint.h>
#include "fsm.h"

// System control registers & NVIC
#define SYSCTL_RCGCGPIO_R   (*((volatile uint32_t *)0x400FE608))  // GPIO clock
#define SYSCTL_RCGCPWM_R    (*((volatile uint32_t *)0x400FE640))  // PWM clock
#define NVIC_EN0_R             (*((volatile uint32_t *)0xE000E100))      // IRQ 0-31 enable

// Register addresses for Port F (base address 0x40025000 + offset)
#define GPIO_PORTF_DATA_R   (*((volatile uint32_t *)0x400253FC)) //   Data register
#define GPIO_PORTF_DIR_R    (*((volatile uint32_t *)0x40025400)) //   Direction register
#define GPIO_PORTF_AFSEL_R  (*((volatile uint32_t *)0x40025420)) //   Alternate function
#define GPIO_PORTF_DEN_R    (*((volatile uint32_t *)0x4002551C)) //   Digital enable
#define GPIO_PORTF_PUR_R    (*((volatile uint32_t *)0x40025510)) //   Pull-up resistor
#define GPIO_PORTF_LOCK_R   (*((volatile uint32_t *)0x40025520)) //   Lock register
#define GPIO_PORTF_CR_R     (*((volatile uint32_t *)0x40025524)) //   Commit register
#define SYSCTL_RCGCGPIO_R   (*((volatile uint32_t *)0x400FE608)) //   Clock control
// Interrupt registers
#define   GPIO_PORTF_IS_R     (*((volatile   uint32_t   *)0x40025404))   //   Int   sense
#define   GPIO_PORTF_IBE_R    (*((volatile   uint32_t   *)0x40025408))   //   Int   both edges
#define   GPIO_PORTF_IEV_R    (*((volatile   uint32_t   *)0x4002540C))   //   Int   event
#define   GPIO_PORTF_IM_R     (*((volatile   uint32_t   *)0x40025410))   //   Int   mask
#define   GPIO_PORTF_ICR_R    (*((volatile   uint32_t   *)0x4002541C))   //   Int   clear

// GPIO Port B registers (base: 0x40005000)
#define GPIO_PORTB_AFSEL_R  (*((volatile uint32_t *)0x40005420))  // Alt function
#define GPIO_PORTB_DEN_R    (*((volatile uint32_t *)0x4000551C))  // Digital enable
#define GPIO_PORTB_AMSEL_R  (*((volatile uint32_t *)0x40005528))  // Analog mode
#define GPIO_PORTB_PCTL_R   (*((volatile uint32_t *)0x4000552C))  // Port control

// PWM Module 0, Generator 0 registers (base: 0x40028000)
#define PWM0_ENABLE_R       (*((volatile uint32_t *)0x40028008))  // PWM output enable
#define PWM0_0_CTL_R        (*((volatile uint32_t *)0x40028040))  // Generator control
#define PWM0_0_LOAD_R       (*((volatile uint32_t *)0x40028050))  // Load (period)
#define PWM0_0_CMPA_R       (*((volatile uint32_t *)0x40028058))  // Compare A (duty)
#define PWM0_0_GENA_R       (*((volatile uint32_t *)0x40028060))  // Generator A action
#define PWM0_0_CVR_R        (*((volatile uint32_t *)0x40028054)) 

// SysTick Addresses
#define SYST_CSR   (*((volatile uint32_t *)0xE000E010)) // SysTick Control and Status Register
#define SYST_RVR   (*((volatile uint32_t *)0xE000E014)) // Reload Value Register
#define SYST_CVR   (*((volatile uint32_t *)0xE000E018)) // Current Value Register

#define RED   (1 << 1)
#define BLUE  (1 << 2)
#define GREEN (1 << 3)
#define SW1   (1 << 4)
#define SW2   (1 << 0)

volatile State_t currentState = IDLE;  // Current state, can take on all possible states
volatile uint16_t songIndex = 0;

// Song to be played, each step is {note, color}
const Step_t song[] = {
    {40, RED, 500}, {40, BLUE, 500},
    {47, GREEN, 500}, {47, RED, 500},
    {49, BLUE, 500}, {49, GREEN, 500},
    {47, RED, 1000}
};

/* --------- INITIALIZATION ----------- */
void PortF_Init_Interrupt(void) {
    SYSCTL_RCGCGPIO_R |= 0x20;                      // Enable clock for Port F
    while ((SYSCTL_RCGCGPIO_R & 0x20) == 0) {}      // Wait for clock

    GPIO_PORTF_LOCK_R = 0x4C4F434B;     //   Unlock Port F
    GPIO_PORTF_CR_R = 0x1F;             //   Allow changes to PF4-PF0
    GPIO_PORTF_DIR_R = 0x0E;            //   PF1-PF3 output, PF4,PF0 input
    GPIO_PORTF_AFSEL_R = 0x00;          //   Disable alternate functions
    GPIO_PORTF_PUR_R = 0x11;            //   Enable pull-up on PF4 and PF0
    GPIO_PORTF_DEN_R = 0x1F;            //   Enable digital I/O on PF4-PF0

    GPIO_PORTF_IS_R    &= ~(SW1 | SW2);         //   Edge-triggered
    GPIO_PORTF_IBE_R   &= ~(SW1 | SW2);         //   Trigger on single edge
    GPIO_PORTF_IEV_R   &= ~(SW1 | SW2);         //   Falling edge
    GPIO_PORTF_ICR_R    =  (SW1 | SW2);         //   Clear pending interrupt
    GPIO_PORTF_IM_R    |=  (SW1 | SW2);         //   Enable interrupt on SW1

    NVIC_EN0_R |= (1 << 30);            // Enable Port F in NVIC
}

void PWM_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x02;   // Enable clock for Port B
    SYSCTL_RCGCPWM_R  |= 0x01;   // Enable clock for PWM Module 0

    while ((SYSCTL_RCGCGPIO_R & 0x02) == 0) {}  // Wait for clock
    while ((SYSCTL_RCGCPWM_R  & 0x01) == 0) {}  // Wait for clock

    // Configure PB6 as PWM output (alternate function 4 = M0PWM0)
    GPIO_PORTB_AFSEL_R |= 0x40;  
    GPIO_PORTB_PCTL_R  = (GPIO_PORTB_PCTL_R & 0xF0FFFFFF) | 0x04000000;
    GPIO_PORTB_DEN_R  |= 0x40;
    GPIO_PORTB_AMSEL_R &= ~0x40;

    // PWM Generator 0: count down, 440 Hz, 50% duty
    PWM0_0_CTL_R  = 0;                               // Disable during setup
    PWM0_0_GENA_R = 0x8C;                            // High at LOAD, low at CMPA
    PWM0_0_LOAD_R = (SYSCLK / TONE_HZ) - 1;          // 440 Hz period
    PWM0_0_CMPA_R = PWM0_0_LOAD_R / 2;               // 50% duty cycle
    PWM0_0_CTL_R  = 1;                               // Enable generator
    PWM0_ENABLE_R &= ~0x01;                          // Begin with PWM on PB6 disabled
}

/* -------------- Interrupt Handlers ---------------- */
void GPIOPortF_Handler(void) {
    /* Switch 1 pressed */
    if (!(GPIO_PORTF_DATA_R & SW1)) {
        if (currentState == IDLE) currentState = PLAY;
        else if (currentState == PLAY) currentState = PAUSE;
        else if (currentState == PAUSE) currentState = PLAY;
    }
    
    /* Switch 2 pressed */
    if (!(GPIO_PORTF_DATA_R & SW2)) {
        currentState = IDLE;
    }
}

/* ------------ GENERAL FUNCTIONS --------------- */
void note(Step_t step) {
    double frequency = 440 * pow(2.0, (step.note - 49) / 12.0); // Get frequency of note
    double load = (SYSCLK / frequency - 1);                     // Get period of note

    if (load > 65535) load = 65535;                          // prevent random overload values
    PWM0_0_LOAD_R = load;                                    // set new period
    PWM0_0_CMPA_R = load / 2;                                // set new duty cycle

    PWM0_ENABLE_R |= 0x01;                                   // enable output
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~0x0E) | step.color;
    delay(step.duration); /* Need to figure out how to do without using CPU time and pausing interrupts*/

    PWM0_ENABLE_R &= ~0x01;                                  // stop sound
    delay(25);
}


void FSM_Update(void) {
    switch (currentState) {
        case IDLE: {
            PWM0_ENABLE_R &= ~(0x01);       // Turn off audio
            GPIO_PORTF_DATA_R &= ~(0x0F);   // Turn off LEDs
            songIndex = 0;                  // Reset song index
            break;
        }

        case PLAY: {            
            Step_t step = song[songIndex];
            playNote(step);
            songIndex++;
            break;
        }


        case PAUSE: {
            PWM0_ENABLE_R &= ~(0x01);  // Turn off audio
            break;
        }
    }
}

int main(void) {
    PortF_Init_Interrupt();
    PWM_Init();

}
