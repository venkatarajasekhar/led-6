#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <pigpio.h>
#include <semaphore.h>

#include "ps.h"

#define RED_PIN                19
#define BLUE_PIN               13
#define GREEN_PIN              26
#define WHITE_PIN              6
#define PWM_RANGE              500
#define PWM_FREQUENCY          800
#define EXP_INVERSE            0.36787944117144f
#define EXP_MINUX_EXP_INVERSE  2.35040238728760f
#define BREATH_LOW_INTENSITY   0.1f

#define RED_COLOR              (color_t) {{1.0}, {0.0}, {0.0}}
#define BLUE_COLOR             (color_t) {{0.0}, {0.0}, {1.0}}
#define GREEN_COLOR            (color_t) {{0.0}, {1.0}, {0.0}}
#define YELLOW_COLOR           (color_t) {{1.0}, {1.0}, {0.0}}
#define MAGENTA_COLOR          (color_t) {{0.0}, {0.0}, {1.0}}
#define CYAN_COLOR             (color_t) {{0.0}, {1.0}, {1.0}}
#define WHITE_COLOR            (color_t) {{1.0}, {1.0}, {1.0}}
#define BLACK_COLOR            (color_t) {{0.0}, {0.0}, {0.0}}

#ifndef MIN
#define MIN(x, y)               ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y)               ((x) > (y) ? (x) : (y))
#endif

typedef struct color {
    union {
        float r;
        float h;
        float s0;
        float red;
        float hue;
    };
    union {
        float g;
        float s;
        float s1;
        float green;
        float saturation;
    };
    union {
        float b;
        float v;
        float l;
        float s2;
        float blue;
        float value;
        float lightness;
    };
    union {
        float w;
        float s3;
        float white;
    };
} color_t;

typedef char colorType;
enum {
    kColorTypeRGB = 0,
    kColorTypeHSV = 1
};

// Global variables
int led_run = 1;
color_t prevHSVColor;
color_t newHSVColor;
color_t newRGBColor;
sem_t lock;

// Function prototypes
void showColorValues(const color_t color, const char *label);
void setColor(const color_t color);
color_t getColor(void);
color_t hsv2rgb(const color_t input);

// Function implementations
void showColorValues(const color_t color, const char *label) {
    printf("%s = %.4f %.4f %.4f\n", label, color.r, color.g, color.b);
}


void setColorInFloat(const int r, const int g, const int b, const int w) {
    gpioPWM(RED_PIN, r);
    gpioPWM(BLUE_PIN, b);
    gpioPWM(GREEN_PIN, g);
    gpioPWM(WHITE_PIN, w);
}


void setColor(const color_t color) {
    return setColorInFloat((int)(PWM_RANGE * color.red),
                           (int)(PWM_RANGE * color.green),
                           (int)(PWM_RANGE * color.blue),
                           (int)(PWM_RANGE * color.white));
}


color_t getColor(void) {
    int r = gpioGetPWMdutycycle(RED_PIN);
    int b = gpioGetPWMdutycycle(BLUE_PIN);
    int g = gpioGetPWMdutycycle(GREEN_PIN);
    int w = gpioGetPWMdutycycle(WHITE_PIN);
    return (color_t){{(float)r / PWM_RANGE}, {(float)g / PWM_RANGE}, {(float)b / PWM_RANGE}, {(float)w / PWM_RANGE}};
}


color_t hsv2rgb(const color_t input) {
    color_t output = {{0.0f}, {0.0f}, {0.0f}, {0.0f}};
    float c = input.v * input.s;
    float x = c * (1.0f - fabs(fmod(input.h * 6.0f, 2.0f) - 1.0));
    // Hue dependent piece-wise function
    if (input.h < 0.16666666666666f) {
        output.r = c; output.g = x; output.b = 0;
    } else if (input.h < 0.33333333333333f) {
        output.r = x; output.g = c; output.b = 0;
    } else if (input.h < 0.50000000000000f) {
        output.r = 0; output.g = c; output.b = x;
    } else if (input.h < 0.66666666666666f) {
        output.r = 0; output.g = x; output.b = c;
    } else if (input.h < 0.83333333333333f) {
        output.r = x; output.g = 0; output.b = c;
    } else {
        output.r = c; output.g = 0; output.b = x;
    }
    // Add some amount to each channel to match lightness
    float m = input.v - c;
    output.r = MIN(output.r + m, 1.0f);
    output.g = MIN(output.g + m, 1.0f);
    output.b = MIN(output.b + m, 1.0f);
    return output;
}

void cycleColor(const useconds_t wait) {
    float k;
    color_t hsvColor = {{0.0f}, {1.0f}, {1.0f}};
    // Increments of 1.0 / 360.0
    for (k = 0.0f; k<1.0f; k += 0.00277777777777f) {
        hsvColor.h = k;
        setColor(hsv2rgb(hsvColor));
        if (!led_run) {
            break;
        }
        usleep(wait);
    }
}

color_t breath(const useconds_t wait, const color_t color) {
    int r = 0, g = 0, b = 0, w = 0;
    float f, k;
    const float I = 1.0f - BREATH_LOW_INTENSITY;
    
    led_run = 1;
    
    for (k = 0.0f; k < 2.0f * M_PI; k += 0.004f * M_PI) {
        f = I * (expf(-cosf(k)) - EXP_INVERSE) / EXP_MINUX_EXP_INVERSE + BREATH_LOW_INTENSITY;
        r = (int)(PWM_RANGE * f * color.red);
        g = (int)(PWM_RANGE * f * color.green);
        b = (int)(PWM_RANGE * f * color.blue);
        w = (int)(PWM_RANGE * f * color.white);
        setColorInFloat(r, g, b, w);
        if (!led_run) {
            break;
        }
        usleep(wait);
    }
    return (color_t){{r}, {g}, {b}};
}

void shiftColor(const useconds_t wait, const color_t colorStart, const color_t colorEnd, const colorType type) {
    float k;
    color_t color, tmp;

    for (k = 0.0f; k <= 1.0f; k += 0.002f) {
        tmp.s0 = colorStart.s0 - k * (colorStart.s0 - colorEnd.s0);
        tmp.s1 = colorStart.s1 - k * (colorStart.s1 - colorEnd.s1);
        tmp.s2 = colorStart.s2 - k * (colorStart.s2 - colorEnd.s2);
        tmp.s3 = colorStart.s3 - k * (colorStart.s3 - colorEnd.s3);
        if (type == kColorTypeHSV) {
            color = hsv2rgb(tmp);
        } else {
            color = tmp;
        }
        // printf("  - k = %.4f  HSV: %.2f %.2f %.2f --> RGB: %d %d %d\n", k, tmp.h, tmp.s, tmp.v, r, g, b);
        setColor(color);
        if (!led_run) {
            break;
        }
        usleep(wait);
    }
}


void catchSignal() {
    printf("Exit nicely ...\n");
    led_run = 0;
}


int handle_command(PS_attendant *A) {
    int k, v;
    char str[8];
    color_t currentRGBColor;
    sem_getvalue(&lock, &v);
    printf("Command %s  (lock = %d)\n", A->cmd, v);
    sem_wait(&lock);
    printf("Processing command ...\n");
    switch (A->cmd[0]) {
        case 'a':
            led_run = 1;
            break;
        case 'c':
        case 'C':
            led_run = 2;
            k = sscanf(A->cmd, "%s %f %f %f %f", str, &newRGBColor.red, &newRGBColor.green, &newRGBColor.blue, &newRGBColor.w);
            if (k == 5) {
                //printf("Shifting to new RGBW color %.2f %.2f %.2f %.2f\n", newRGBColor.red, newRGBColor.green, newRGBColor.blue, newRGBColor.white);
                if (A->cmd[0] == 'c') {
                    currentRGBColor = getColor();
                    shiftColor(1000, currentRGBColor, newRGBColor, kColorTypeRGB);
                } else {
                    setColor(newRGBColor);
                }
            } else {
                printf("Incomplete command '%s'.\n", A->cmd);
            }
            break;
        default:
            break;
    }
    sem_post(&lock);
    return 0;
}

int main(int argc, char *argv[]) {
    
    // Initialize the GPIO library
    if (gpioInitialise() < 0) {
        return EXIT_FAILURE;
    }
    gpioSetPWMrange(6, PWM_RANGE);
    gpioSetMode(6, PI_OUTPUT);
    gpioPWM(6, 1);
    
    // Prepare output pins
    gpioSetPWMrange(RED_PIN,   PWM_RANGE);
    gpioSetPWMrange(GREEN_PIN, PWM_RANGE);
    gpioSetPWMrange(BLUE_PIN,  PWM_RANGE);
    gpioSetMode(RED_PIN,   PI_OUTPUT);
    gpioSetMode(GREEN_PIN, PI_OUTPUT);
    gpioSetMode(BLUE_PIN,  PI_OUTPUT);
    gpioSetPWMfrequency(RED_PIN,   PWM_FREQUENCY);
    gpioSetPWMfrequency(GREEN_PIN, PWM_FREQUENCY);
    gpioSetPWMfrequency(BLUE_PIN,  PWM_FREQUENCY);

    // Catch some signals for graceful exit
    signal(SIGINT,  catchSignal);
    signal(SIGTERM, catchSignal);
    signal(SIGKILL, catchSignal);
    signal(SIGQUIT, catchSignal);

    // Some color variables
    prevHSVColor.h = 0.0f;    prevHSVColor.s = 1.0f;    prevHSVColor.v = 1.0;
    newHSVColor.h  = 0.0f;     newHSVColor.s = 1.0f;     newHSVColor.v = 1.0;
    newRGBColor  = hsv2rgb(newHSVColor);

    if (argc > 1) {
        float h = atof(argv[1]);
        printf("Start with hue = %.2f\n", h);
        newHSVColor.h = h;
    }

    // Cycle through the color wheel
    //cycleColor(3000);

    shiftColor(1000,
               BLACK_COLOR,
               hsv2rgb((color_t){{newHSVColor.h}, {1.0f}, {BREATH_LOW_INTENSITY}}),
               kColorTypeRGB);
    
    // Keep a copy as "previous"
    prevHSVColor.hue = newHSVColor.hue;

//    // Fade to black
//    shiftColor(500, getColor(), BLACK_COLOR, kColorTypeRGB);
//    
//    // Conclude the GPIO library
//    gpioTerminate();
//    return 0;
    
    sem_init(&lock, 0, 1);

    PS_server *S = PS_init();
    PS_set_terminate_function_to_builtin(S);
    PS_set_command_function(S, &handle_command);
    PS_set_name_and_logfile(S, "LED", "led.log");
    S->port = 10000;
    PS_run(S);
    
    //shiftColor(1000, getColor(), CYAN_COLOR, kColorTypeRGB);

    while (led_run) {
        if (led_run == 1) {
            sem_wait(&lock);

            newHSVColor.hue = fmodf(newHSVColor.hue + 0.1f * (float)(rand()) / RAND_MAX, 1.0f);
            
            newRGBColor = hsv2rgb(newHSVColor);
            
            //shiftColor(2000, prevHSVColor, newHSVColor, kColorTypeHSV);
            
            shiftColor(1000,
                       hsv2rgb((color_t){{prevHSVColor.h}, {1.0f}, {BREATH_LOW_INTENSITY}}),
                       hsv2rgb((color_t){{newHSVColor.h}, {1.0f}, {BREATH_LOW_INTENSITY}}),
                       kColorTypeRGB);
            
            breath(10000, newRGBColor);
            
            prevHSVColor.hue = newHSVColor.hue;

            sem_post(&lock);
            
            usleep(10);
        } else {
            sleep(1);
        }
    }
    
    sem_destroy(&lock);
    
    // Fade to black
    shiftColor(1000, getColor(), BLACK_COLOR, kColorTypeRGB);

    // Conclude the GPIO library
    gpioTerminate();
    
    return EXIT_SUCCESS;
}

