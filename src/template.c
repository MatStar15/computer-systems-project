
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 

#define MESSAGE_BUFFER_LENGTH 255

//Add here necessary states
enum state { SENDING=1, READY_TO_SEND, READING };
enum state programState = READING;

enum orientation { VERTICAL=1, HORIZONTAL};
enum orientation orientationState = HORIZONTAL; 

bool button1Pressed = false;
bool button2Pressed = false;

uint16_t messageCounter = 0;
char messageBuffer[MESSAGE_BUFFER_LENGTH];



static void example_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // Modify with application code here.

        printf("yo\n");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void data_task(void *arg){
    (void)arg;

    ICM42670_start_with_default_values();

    float ax, ay, az, gx, gy, gz, t;

    for(;;){
        tight_loop_contents(); // Modify with application code here.

        ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t);

        if(gx > 100){
            orientationState = VERTICAL;
        }

        if(gx < -100){
            orientationState = HORIZONTAL;
        }

        // printf("gx: %f, orientation: %d\n", gx, orientationState);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void usb_sending_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // Modify with application code here.

        // Do stuff only if a message is ready to send
        if(programState == READY_TO_SEND){
            programState = SENDING;

            // Add null terminator and send the message
            messageBuffer[messageCounter] = '\0';
            printf("%s\n", messageBuffer);

            // Indicate message sent by blinking LED 3 times
            blink_led(3);

            // Reset for the next message
            messageCounter = 0;
            memset(messageBuffer, 0, MESSAGE_BUFFER_LENGTH);

            // Set state back to READING
            programState = READING;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void usb_receiving_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // Modify with application code here.

        printf("usb_receiving_task\n");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void audio_receiving_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // Modify with application code here.

        printf("audio_receiving_task\n");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void button_task(void *arg){
    (void)arg;
    static uint8_t spaceCounter = 0;

    for(;;){
        tight_loop_contents(); // Modify with application code here.

        // Guard clause, if not in READING state, skip the rest of the loop
        if(programState != READING){
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }

        if(button1Pressed){
            if(orientationState == HORIZONTAL){
                messageBuffer[messageCounter] = '-';
                messageCounter++;
            } else {
                messageBuffer[messageCounter] = '.';
                messageCounter++;
            }
            spaceCounter = 0;

            button1Pressed = false;
        }

        if(button2Pressed){
            messageBuffer[messageCounter] = ' ';
            messageCounter++;
            spaceCounter++;

            // 3 consecutive spaces indicate the end of the message, set state to READY_TO_SEND
            if(spaceCounter >= 3){
                programState = READY_TO_SEND;
                spaceCounter = 0;
            }

            button2Pressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

static void button1Interrupt(uint gpio, uint32_t eventMask) {
    button1Pressed = true;
}

static void button2Interrupt(uint gpio, uint32_t eventMask) {
    button2Pressed = true;
}

int main() {
    stdio_init_all();
    // Uncomment this lines if you want to wait till the serial monitor is connected
    /*while (!stdio_usb_connected()){
        sleep_ms(10);
    }*/ 

    

    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.

    init_button1();
    init_button2();

    init_led();

    init_ICM42670();

    TaskHandle_t dataTaskHandle = NULL;
    // Create the tasks with xTaskCreate
    BaseType_t dataTaskResult = xTaskCreate(data_task,       // (en) Task function
                "data_test",              // (en) Name of the task 
                DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,               // (en) Arguments of the task 
                2,                  // (en) Priority of this task
                &dataTaskHandle);    // (en) A handle to control the execution of this task

    if(dataTaskResult != pdPASS) {
        printf("Data task creation failed\n");
        return 0;
    }

    TaskHandle_t buttonTaskHandle = NULL;
    BaseType_t buttonTaskResult = xTaskCreate(button_task, 
        "button_task", 
        DEFAULT_STACK_SIZE, 
        NULL, 
        2, 
        &buttonTaskHandle);

    if(buttonTaskResult != pdPASS) {
        printf("Button task creation failed\n");
        return 0;
    }


    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, button1Interrupt);
    // gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, button2Interrupt);


    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}

