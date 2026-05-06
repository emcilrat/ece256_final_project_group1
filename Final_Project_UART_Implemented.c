#include <stdint.h>
#include <math.h>
#include "fsm.h"

/* =============================================================================
 * Register Definitions
 * ============================================================================= */

// System Control
#define SYSCTL_RCGCGPIO_R   (*((volatile uint32_t *)0x400FE608))
#define SYSCTL_RCGCPWM_R    (*((volatile uint32_t *)0x400FE640))
#define SYSCTL_RCGCUART_R   (*((volatile uint32_t *)0x400FE618))

// NVIC
#define NVIC_EN0_R          (*((volatile uint32_t *)0xE000E100))

// Port A
#define GPIO_PORTA_AFSEL_R  (*((volatile uint32_t *)0x40004420))
#define GPIO_PORTA_DEN_R    (*((volatile uint32_t *)0x4000451C))
#define GPIO_PORTA_PCTL_R   (*((volatile uint32_t *)0x4000452C))

// Port B
#define GPIO_PORTB_AFSEL_R  (*((volatile uint32_t *)0x40005420))
#define GPIO_PORTB_DEN_R    (*((volatile uint32_t *)0x4000551C))
#define GPIO_PORTB_AMSEL_R  (*((volatile uint32_t *)0x40005528))
#define GPIO_PORTB_PCTL_R   (*((volatile uint32_t *)0x4000552C))

// Port F
#define GPIO_PORTF_DATA_R   (*((volatile uint32_t *)0x400253FC))
#define GPIO_PORTF_DIR_R    (*((volatile uint32_t *)0x40025400))
#define GPIO_PORTF_AFSEL_R  (*((volatile uint32_t *)0x40025420))
#define GPIO_PORTF_DEN_R    (*((volatile uint32_t *)0x4002551C))
#define GPIO_PORTF_PUR_R    (*((volatile uint32_t *)0x40025510))
#define GPIO_PORTF_LOCK_R   (*((volatile uint32_t *)0x40025520))
#define GPIO_PORTF_CR_R     (*((volatile uint32_t *)0x40025524))
#define GPIO_PORTF_IS_R     (*((volatile uint32_t *)0x40025404))
#define GPIO_PORTF_IBE_R    (*((volatile uint32_t *)0x40025408))
#define GPIO_PORTF_IEV_R    (*((volatile uint32_t *)0x4002540C))
#define GPIO_PORTF_IM_R     (*((volatile uint32_t *)0x40025410))
#define GPIO_PORTF_ICR_R    (*((volatile uint32_t *)0x4002541C))

// UART0
#define UART0_DATA_R        (*((volatile uint32_t *)0x4000C000))
#define UART0_FLAG_R        (*((volatile uint32_t *)0x4000C018))
#define UART0_IBRD_R        (*((volatile uint32_t *)0x4000C024))
#define UART0_FBRD_R        (*((volatile uint32_t *)0x4000C028))
#define UART0_LCRH_R        (*((volatile uint32_t *)0x4000C02C))
#define UART0_CTL_R         (*((volatile uint32_t *)0x4000C030))
#define UART0_IM_R          (*((volatile uint32_t *)0x4000C038))
#define UART0_ICR_R         (*((volatile uint32_t *)0x4000C044))

// PWM Module 0, Generator 0
#define PWM0_ENABLE_R       (*((volatile uint32_t *)0x40028008))
#define PWM0_0_CTL_R        (*((volatile uint32_t *)0x40028040))
#define PWM0_0_LOAD_R       (*((volatile uint32_t *)0x40028050))
#define PWM0_0_CMPA_R       (*((volatile uint32_t *)0x40028058))
#define PWM0_0_GENA_R       (*((volatile uint32_t *)0x40028060))

// SysTick
#define SYST_CSR            (*((volatile uint32_t *)0xE000E010))
#define SYST_RVR            (*((volatile uint32_t *)0xE000E014))
#define SYST_CVR            (*((volatile uint32_t *)0xE000E018))

/* =============================================================================
 * Constants and Pin Definitions
 * ============================================================================= */

#define SYSCLK      16000000
#define FS          1000
#define NOTE_GAP_MS 30

#define RED    (1 << 1)
#define BLUE   (1 << 2)
#define GREEN  (1 << 3)
#define COLORS (RED | BLUE | GREEN)
#define SW1    (1 << 4)
#define SW2    (1 << 0)

/* =============================================================================
 * Song Data
 * ============================================================================= */

const Step_t song[] =
{
    {40, RED,   500}, {40, BLUE,  500},
    {47, GREEN, 500}, {47, RED,   500},
    {49, BLUE,  500}, {49, GREEN, 500},
    {47, RED,  1000}
};

#define SONG_LEN (sizeof(song) / sizeof(song[0]))

/* =============================================================================
 * State Variables
 * ============================================================================= */

static volatile State_t currentState = IDLE;
static volatile uint16_t songIndex  = 0;
static volatile uint16_t noteTimer  = 0;
static volatile uint16_t gapTimer   = 0;
static volatile uint8_t  noteActive = 0;
static volatile uint8_t  paused     = 0;

/* =============================================================================
 * State Transition Helpers
 * ============================================================================= */

static void togglePlayPause(void)
{
    if (currentState == IDLE || currentState == PAUSE)
    {
        currentState = PLAY;
        UART0_SendString("Current State: PLAY\r\n");
    }
    else
    {
        currentState = PAUSE;
        UART0_SendString("Current State: PAUSE\r\n");
    }
}

static void resetToIdle(void)
{
    currentState = IDLE;
    UART0_SendString("Current State: IDLE\r\n");
}

/* =============================================================================
 * Initialization
 * ============================================================================= */

void SysTick_Init(void)
{
    SYST_CSR = 0;
    SYST_RVR = (SYSCLK / FS) - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x07;
}

void UART0_Init(void)
{
    SYSCTL_RCGCUART_R |= 0x01;
    SYSCTL_RCGCGPIO_R |= 0x01;
    while (!(SYSCTL_RCGCUART_R & 0x01)) {}
    while (!(SYSCTL_RCGCGPIO_R & 0x01)) {}

    GPIO_PORTA_AFSEL_R |= 0x03;
    GPIO_PORTA_PCTL_R  |= 0x11;
    GPIO_PORTA_DEN_R   |= 0x03;

    UART0_CTL_R  &= ~0x01;
    UART0_IBRD_R  = 8;
    UART0_FBRD_R  = 44;
    UART0_LCRH_R  = 0x60;
    UART0_IM_R   |= 0x10;
    UART0_CTL_R  |= 0x301;

    NVIC_EN0_R |= (1 << 5);
}

void PortF_Init_Interrupt(void)
{
    SYSCTL_RCGCGPIO_R |= 0x20;
    while (!(SYSCTL_RCGCGPIO_R & 0x20)) {}

    GPIO_PORTF_LOCK_R  = 0x4C4F434B;
    GPIO_PORTF_CR_R    = 0x1F;
    GPIO_PORTF_DIR_R   = 0x0E;
    GPIO_PORTF_AFSEL_R = 0x00;
    GPIO_PORTF_PUR_R   = 0x11;
    GPIO_PORTF_DEN_R   = 0x1F;

    GPIO_PORTF_IS_R  &= ~(SW1 | SW2);
    GPIO_PORTF_IBE_R &= ~(SW1 | SW2);
    GPIO_PORTF_IEV_R &= ~(SW1 | SW2);
    GPIO_PORTF_ICR_R  =  (SW1 | SW2);
    GPIO_PORTF_IM_R  |=  (SW1 | SW2);

    NVIC_EN0_R |= (1 << 30);
}

void PWM_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x02;
    SYSCTL_RCGCPWM_R  |= 0x01;
    while (!(SYSCTL_RCGCGPIO_R & 0x02)) {}
    while (!(SYSCTL_RCGCPWM_R  & 0x01)) {}

    GPIO_PORTB_AFSEL_R |=  0x40;
    GPIO_PORTB_PCTL_R   = (GPIO_PORTB_PCTL_R & 0xF0FFFFFF) | 0x04000000;
    GPIO_PORTB_DEN_R   |=  0x40;
    GPIO_PORTB_AMSEL_R &= ~0x40;

    PWM0_0_CTL_R  = 0;
    PWM0_0_GENA_R = 0x8C;
    PWM0_0_LOAD_R = 0;
    PWM0_0_CMPA_R = 0;
    PWM0_0_CTL_R  = 1;
    PWM0_ENABLE_R &= ~0x01;
}

/* =============================================================================
 * Interrupt Handlers
 * ============================================================================= */

void UART0_Handler(void)
{
    char c = UART0_DATA_R & 0xFF;
    UART0_ICR_R = 0x10;

    if (c == 'p' || c == ' ')  togglePlayPause();
    else if (c == 'r')         resetToIdle();
}

void SysTick_Handler(void)
{
    if (paused) return;

    if (noteActive)
    {
        if (noteTimer > 0)  noteTimer--;
        else
        {
            PWM0_ENABLE_R &= ~0x01;
            noteActive = 0;
            gapTimer = NOTE_GAP_MS;
        }
    }
    else if (gapTimer > 0)
    {
        gapTimer--;
    }
}

void GPIOF_Handler(void)
{
    if (!(GPIO_PORTF_DATA_R & SW1))  togglePlayPause();
    if (!(GPIO_PORTF_DATA_R & SW2))  resetToIdle();
    GPIO_PORTF_ICR_R = (SW1 | SW2);
}

/* =============================================================================
 * General Functions
 * ============================================================================= */

void UART0_SendChar(char c)
{
    while (UART0_FLAG_R & (1 << 5)) {}
    UART0_DATA_R = c;
}

void UART0_SendString(const char *str)
{
    while (*str)
        UART0_SendChar(*str++);
}

void playNote(Step_t step)
{
    uint32_t load = (uint32_t)(SYSCLK / (440.0 * exp2((step.note - 49) / 12.0))) - 1;
    if (load < 2)     load = 2;
    if (load > 65535) load = 65535;

    PWM0_0_LOAD_R = load;
    PWM0_0_CMPA_R = load / 2;
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~COLORS) | step.color;
    PWM0_ENABLE_R |= 0x01;

    noteTimer  = (step.duration > NOTE_GAP_MS) ? (step.duration - NOTE_GAP_MS) : step.duration;
    noteActive = 1;
}

/* =============================================================================
 * FSM Update
 * ============================================================================= */

void FSM_Update(void)
{
    switch (currentState)
    {
        case IDLE:
            PWM0_ENABLE_R     &= ~0x01;
            GPIO_PORTF_DATA_R &= ~COLORS;
            songIndex = noteActive = gapTimer = noteTimer = paused = 0;
            break;

        case PLAY:
            if (paused) { paused = 0; PWM0_ENABLE_R |= 0x01; }
            if (!noteActive && gapTimer == 0)
            {
                if (songIndex >= SONG_LEN) { resetToIdle(); break; }
                playNote(song[songIndex++]);
            }
            break;

        case PAUSE:
            PWM0_ENABLE_R &= ~0x01;
            paused = 1;
            break;
    }
}

/* =============================================================================
 * Entry Point
 * ============================================================================= */

int main(void)
{
    PortF_Init_Interrupt();
    PWM_Init();
    SysTick_Init();
    UART0_Init();

    while (1) FSM_Update();
}
