/* Step of song */
typedef struct {
    uint16_t note;         // Note number
    uint8_t color;         // Color of LED
    uint16_t duration;     // Duration of note
} Step_t;

/* States in FSM */
typedef enum {
    IDLE,            
    PLAY,
    PAUSE
} State_t;

/* Update state */
void FSM_Update(void);