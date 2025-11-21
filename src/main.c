
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
#define RX_COMMAND_BUFFER_LENGTH 64

//Add here necessary states
enum state { SENDING=1, READY_TO_SEND, READING, PROCESSING_MESSAGE };
enum state programState = READING;

enum orientation { VERTICAL=1, HORIZONTAL};
enum orientation orientationState = HORIZONTAL; 

bool button1Pressed = false;
bool button2Pressed = false;

uint16_t messageCounter = 0;
char messageBuffer[MESSAGE_BUFFER_LENGTH];

static void play_buzzer(char *message);
static void translate_morse2alpha(char *morseMessage, char *alphaMessage, uint8_t messageLength);


static void data_task(void *arg){
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10000));

    ICM42670_start_with_default_values();
    // printf("ICM42670 initialized\n");


    float ax, ay, az, gx, gy, gz, t;

    for(;;){

        if (programState != READING){
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

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

    // Buffer to store incoming commands
    char rxBuffer[RX_COMMAND_BUFFER_LENGTH];
    uint16_t rxCounter = 0;

    for(;;){
        int c = getchar();

        if (c != EOF) {
            
            if (c == '\n' || c == '\r') {
                rxBuffer[rxCounter] = '\0';

                if (rxCounter > 0) {

                    programState = PROCESSING_MESSAGE;
                    
                    // Plan: "check /a" -> "to LED"
                    if (strcmp(rxBuffer, "/a") == 0) {
                        // Toggle the LED
                        
                    }
                    else if (strcmp(rxBuffer, "/b") == 0) {
                        // Play a 500Hz note for 200ms
                        // play_buzzer_note(500, 200);
                        printf("Received: /b. Playing buzzer.\n");
                    } 
                    else{
                        // Write message to lcd screen
                        clear_display();

                        char alphaMessage[MESSAGE_BUFFER_LENGTH];
                        translate_morse2alpha(rxBuffer, alphaMessage, rxCounter);
                        // printf("Translated message: %s\n", alphaMessage);
                        write_text(alphaMessage);

                        play_buzzer(rxBuffer);
                    }

                    programState = READING;
                }

                rxCounter = 0;
                memset(rxBuffer, 0, RX_COMMAND_BUFFER_LENGTH);

            } 
            // If it's a regular character, add it to the buffer
            else {
                if (rxCounter < (RX_COMMAND_BUFFER_LENGTH - 1)) {
                    rxBuffer[rxCounter] = (char)c;
                    rxCounter++;
                } else {
                    // Buffer overflow, reset
                    rxCounter = 0;
                    memset(rxBuffer, 0, RX_COMMAND_BUFFER_LENGTH);
                }
            }
        }
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
                buzzer_play_tone(440, 300);
            } else {
                messageBuffer[messageCounter] = '.';
                messageCounter++;
                buzzer_play_tone(440, 100);
            }
            spaceCounter = 0;

            button1Pressed = false;
            // printf("%c`", messageBuffer[messageCounter-1]);
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
            // printf("%c`", messageBuffer[messageCounter-1]);
        }
        
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

static void gpio_callback(uint gpio, uint32_t events) {
    // printf("GPIO Interrupt on pin %d, events: %u\n", gpio, events);
    if (gpio == BUTTON1  && (events ==  8)) {
        button1Pressed = true;
        // printf("Button 1 Interrupt\n");
    }
    if (gpio == BUTTON2 && (events ==  8)) {
        button2Pressed = true;
        // printf("Button 2 Interrupt\n");
    }
}

//   translate morse to alphabet
static void translate_morse2alpha(char *morseMessage, char *alphaMessage, uint8_t messageLength){
    static char *letter = "  ETIANMSURWDKGOHVF?L?PJBXCYZQ??";

    // printf("Translating morse message: %s\n", morseMessage);
    // printf("Message length: %d\n", messageLength);

    uint8_t counter = 0;
    uint8_t index = 1;
    for (size_t i = 0; i < messageLength; i++){
	    if (morseMessage[i] == '-'){
		    index = (index * 2) + 1;
	    }
	    else if (morseMessage[i] == '.'){
		    index = index * 2;
	    }
	    else{
		    alphaMessage[counter] = letter[index];
		    counter++;
		    index = 1;
	    }
    }


    alphaMessage[counter] = letter[index];
    alphaMessage[counter + 1] = '\0';
}

static void play_buzzer(char *string){
    
    char *ptr;

    for (ptr = string; *ptr != 0; ptr++) {
        if (*ptr == '-') {
            buzzer_play_tone(440, 300);
        } else if (*ptr == '.') {
            buzzer_play_tone(440, 100);
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));                    
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
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

    init_i2c_default();
    init_display();
    clear_display();

    init_buzzer();

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

    TaskHandle_t usbSendingTaskHandle = NULL;
    BaseType_t usbSendingTaskResult = xTaskCreate(usb_sending_task,
        "usb_sending_task",
        DEFAULT_STACK_SIZE,
        NULL,
        2,
        &usbSendingTaskHandle);

    if(usbSendingTaskResult != pdPASS) {
        printf("USB sending task creation failed\n");
        return 0;
    }

    TaskHandle_t usbReceivingTaskHandle = NULL;
    BaseType_t usbReceivingTaskResult = xTaskCreate(usb_receiving_task,
        "usb_receiving_task",
        DEFAULT_STACK_SIZE,
        NULL,
        2,
        &usbReceivingTaskHandle);
    if(usbReceivingTaskResult != pdPASS) {
        printf("USB receiving task creation failed\n");
        return 0;
    }


    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, gpio_callback);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, gpio_callback);


    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}
