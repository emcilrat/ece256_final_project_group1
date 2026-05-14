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
#define GPIO_PORTB_DATA_R   (*((volatile uint32_t *)0x400053FC))
#define GPIO_PORTB_DIR_R    (*((volatile uint32_t *)0x40005400))
#define GPIO_PORTB_AFSEL_R  (*((volatile uint32_t *)0x40005420))
#define GPIO_PORTB_DEN_R    (*((volatile uint32_t *)0x4000551C))
#define GPIO_PORTB_AMSEL_R  (*((volatile uint32_t *)0x40005528))
#define GPIO_PORTB_PCTL_R   (*((volatile uint32_t *)0x4000552C))

// Port F
#define GPIO_PORTF_DATA_R   (*((volatile uint32_t *)0x400253FC))
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
#define SYSCTL_RCC_R        (*((volatile uint32_t *)0x400FE060))

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

#define SW1    (1 << 4)
#define SW2    (1 << 0)

#define SR_SER_PIN   (1 << 3)  // PB3 - Serial Data
#define SR_SRCLK_PIN (1 << 4)  // PB4 - Shift Clock
#define SR_RCLK_PIN  (1 << 5)  // PB5 - Latch Clock

#define SR_SER_LOW()    (GPIO_PORTB_DATA_R &= ~SR_SER_PIN)
#define SR_SER_HIGH()   (GPIO_PORTB_DATA_R |=  SR_SER_PIN)
#define SR_SRCLK_LOW()  (GPIO_PORTB_DATA_R &= ~SR_SRCLK_PIN)
#define SR_SRCLK_HIGH() (GPIO_PORTB_DATA_R |=  SR_SRCLK_PIN)
#define SR_RCLK_LOW()   (GPIO_PORTB_DATA_R &= ~SR_RCLK_PIN)
#define SR_RCLK_HIGH()  (GPIO_PORTB_DATA_R |=  SR_RCLK_PIN)

/* =============================================================================
 * Audio Data
 * ============================================================================= */

const Step_t song[] = {
    // --- Phrase 1: "Deck the halls with boughs of holly" ---
    {67, 0x1, 300}, {65, 0x2, 100}, {64, 0x1, 200}, {62, 0x2, 200}, // G, F, E, D
    {60, 0x1, 200}, {62, 0x2, 200}, {64, 0x1, 200}, {60, 0x2, 200}, // C, D, E, C

    // --- The "Fa-la-la" (FAST STROBE) ---
    {62, 0x1, 100}, {64, 0x2, 100}, {65, 0x1, 100}, {62, 0x2, 100},
    {64, 0x1, 200}, {62, 0x2, 200}, {60, 0x1, 200}, {59, 0x2, 200},
    {60, 0x1, 400},

    // --- Phrase 2: "Tis the season to be jolly" ---
    {67, 0x2, 300}, {65, 0x1, 100}, {64, 0x2, 200}, {62, 0x1, 200},
    {60, 0x2, 200}, {62, 0x1, 200}, {64, 0x2, 200}, {60, 0x1, 200},

    // --- The "Fa-la-la" (FAST STROBE 2) ---
    {62, 0x2, 100}, {64, 0x1, 100}, {65, 0x2, 100}, {62, 0x1, 100},
    {64, 0x2, 200}, {62, 0x1, 200}, {60, 0x2, 200}, {59, 0x1, 200},
    {60, 0x2, 400},

    // --- The Bridge: "Don we now our gay apparel" ---
    {62, 0x1, 300}, {64, 0x2, 100}, {65, 0x1, 200}, {62, 0x2, 200},
    {64, 0x1, 300}, {65, 0x2, 100}, {67, 0x1, 200}, {62, 0x2, 200},
    {64, 0x1, 100}, {66, 0x2, 100}, {67, 0x1, 200}, {69, 0x2, 100}, {71, 0x1, 100},
    {72, 0x2, 400},

    // --- FINALE: reprise Phrase 1, then ride the climb out ---
    {67, 0x1, 300}, {65, 0x2, 100}, {64, 0x1, 200}, {62, 0x2, 200}, // G, F, E, D
    {60, 0x1, 200}, {62, 0x2, 200}, {64, 0x1, 200}, {60, 0x2, 200}, // C, D, E, C

    // --- Final climb — bigger than the bridge ---
    {62, 0x1, 100}, {64, 0x2, 100}, {65, 0x1, 100}, {67, 0x2, 100},
    {69, 0x1, 100}, {71, 0x2, 100}, {72, 0x1, 100}, {74, 0x2, 100},
    {76, 0x1, 800}
};

const Step_t scare_crash[] = {
    {60, 0x01, 60}, {50, 0x6, 60}, {40, 0x01, 80},
    {30, 0x06, 100}, {20, 0x05, 150}, {10, 0x02, 300}
};

const Step_t scare_stinger[] = {
    {72, 0x01, 50}, {78, 0x06, 50}, {72, 0x05, 50},
    {78, 0x02, 50}, {72, 0x01, 50}, {78, 0x06, 50},
    {84, 0x05, 400}
};

const Step_t scare_creep[] = {
    {28, 0x01, 250}, {29, 0x6, 250}, {28, 0x01, 250},
    {29, 0x06, 250}, {25, 0x5, 500}, {20, 0x02, 800}
};

#define SONG_LEN (sizeof(song) / sizeof(song[0]))

/* =============================================================================
 * State Variables
 * ============================================================================= */

static volatile State_t  currentState = IDLE;
static volatile uint16_t songIndex    = 0;
static volatile uint16_t noteTimer    = 0;
static volatile uint16_t gapTimer     = 0;
static volatile uint8_t  noteActive   = 0;
static volatile uint8_t  paused       = 0;

static const Step_t* currentScareArray;
static uint8_t scareLen   = 0;
static uint8_t scareIndex = 0;

static int16_t grinchBrightness = 0;
static int8_t  fadeDirection    = 1;
static uint16_t fadeTimer       = 0;

static volatile uint8_t srState = 0x00;

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
    else if (currentState == PLAY)
    {
        uint32_t r = SYST_CVR % 3;
        if      (r == 0) { currentScareArray = scare_crash;   scareLen = 6; }
        else if (r == 1) { currentScareArray = scare_stinger; scareLen = 7; }
        else             { currentScareArray = scare_creep;   scareLen = 6; }

        scareIndex = 0;
        currentState = SCARE;
        UART0_SendString("Current State: SCARE\r\n");
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

    SYSCTL_RCC_R &= ~0x001E0000;
    SYSCTL_RCC_R |=  0x001E0000;

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
    static uint16_t grinchPWM = 0;

    // --- 1. Song timing ---
    if (!paused) {
        if (noteActive) {
            if (noteTimer > 0) noteTimer--;
            else {
                PWM0_ENABLE_R &= ~0x01;
                noteActive = 0;
                gapTimer = NOTE_GAP_MS;
            }
        } else if (gapTimer > 0) {
            gapTimer--;
        }
    }

    // --- 2. Grinch LED soft-PWM fade (PAUSE state only) ---
    if (currentState == PAUSE) {
        grinchPWM++;
        if (grinchPWM >= 10) grinchPWM = 0;

        if (grinchPWM < grinchBrightness) {
            srState |= 0x08;
        } else {
            srState &= ~0x08;
        }
        ShiftReg_Send(srState);

        fadeTimer++;
        if (fadeTimer >= 50) {
            fadeTimer = 0;
            grinchBrightness += fadeDirection;
            if (grinchBrightness >= 10 || grinchBrightness <= 0) fadeDirection *= -1;
        }
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
    double freq = 440.0 * pow(2.0, (step.note - 49) / 12.0);
    uint32_t load = (uint32_t)(250000.0 / freq) - 1;

    if (load < 2)     load = 2;
    if (load > 65535) load = 65535;

    PWM0_0_LOAD_R = load;
    PWM0_0_CMPA_R = load / 2;

    srState = step.data;
    ShiftReg_Send(srState);

    PWM0_ENABLE_R |= 0x01;
    noteTimer = (step.duration > NOTE_GAP_MS) ? (step.duration - NOTE_GAP_MS) : step.duration;
    noteActive = 1;
}

void ShiftReg_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x02;
    while (!(SYSCTL_RCGCGPIO_R & 0x02)) {}

    GPIO_PORTB_DIR_R   |=  (SR_SER_PIN | SR_SRCLK_PIN | SR_RCLK_PIN);
    GPIO_PORTB_DEN_R   |=  (SR_SER_PIN | SR_SRCLK_PIN | SR_RCLK_PIN);
    GPIO_PORTB_AFSEL_R &= ~(SR_SER_PIN | SR_SRCLK_PIN | SR_RCLK_PIN);

    SR_SER_LOW();
    SR_SRCLK_LOW();
    SR_RCLK_LOW();
    ShiftReg_Send(0x00);
}

void ShiftReg_Send(uint8_t data)
{
    SR_RCLK_LOW();
    int i;
    for (i = 7; i >= 0; i--)
    {
        SR_SRCLK_LOW();
        if (data & (1 << i)) SR_SER_HIGH();
        else                  SR_SER_LOW();
        SR_SRCLK_HIGH();
    }
    SR_RCLK_HIGH();
    SR_RCLK_LOW();
}

/* =============================================================================
 * FSM Update
 * ============================================================================= */

void FSM_Update(void)
{
    switch (currentState)
    {
        case IDLE:
            PWM0_ENABLE_R &= ~0x01;
            srState = 0x00;
            ShiftReg_Send(srState);
            songIndex = noteActive = gapTimer = noteTimer = paused = 0;
            break;

        case PLAY:
            if (paused) { paused = 0; PWM0_ENABLE_R |= 0x01; }
            if (!noteActive && gapTimer == 0)
            {
                if (songIndex >= SONG_LEN) { resetToIdle(); break; }
                playNote(song[songIndex++]);
            }
            srState |= (1 << 2);
            ShiftReg_Send(srState);
            break;

        case SCARE:
            if (!noteActive && gapTimer == 0)
            {
                if (scareIndex >= scareLen)
                {
                    UART0_SendString("Current State: PAUSE\r\n");
                    currentState = PAUSE;
                    break;
                }
                playNote(currentScareArray[scareIndex++]);
            }
            break;

        case PAUSE:
            if (!paused) {
                PWM0_ENABLE_R &= ~0x01;
                srState = 0x00;
                ShiftReg_Send(srState);
                paused = 1;
                grinchBrightness = 0;
                fadeDirection = 1;
            }
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
    ShiftReg_Init();
    SysTick_Init();
    UART0_Init();

    srState = 15;
    ShiftReg_Send(srState);

    while (1) FSM_Update();
}
