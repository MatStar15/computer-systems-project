
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

/*
Project created by:
Mikołaj Nawrot
Siemke Van Reyn
Matviy Starodubtsev
*/

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 

#define MESSAGE_BUFFER_LENGTH 255
#define RX_COMMAND_BUFFER_LENGTH 64

//Program states
enum state { SENDING=1, READY_TO_SEND, READING, PROCESSING_MESSAGE, IN_MENU, PLAYING_SONG };
enum state programState = READING;

//Orientation states
enum orientation { HORIZONTAL=1, VERTICAL};
enum orientation orientationState = HORIZONTAL; 

bool button1Pressed = false;
bool button2Pressed = false;

// Message buffer
uint16_t messageCounter = 0;
char messageBuffer[MESSAGE_BUFFER_LENGTH];

// Menu options
char menuOptions[5][10] = {"SELECT", "SONG 1", "SONG 2", "SONG 3", "EXIT"};
uint8_t currentMenuOption = 0;

// Menu songs (stolen from AI)
int16_t song1[] = {
    330, 294, 262, 294, 330, 330, 330, 0,    /* E D C D E E E - pause */
    294, 294, 294, 330, 392, 392, 0,         /* D D D E G G - pause */
    330, 294, 262, 294, 330, 330, 330, 0,    /* E D C D E E E - pause */
    330, 294, 294, 330, 294, 262, -1         /* E D D E D C */
};
int16_t song2[] = {
    392, 392, 440, 392, 523, 494, 0,         /* Hap-py birth-day to you - pause */
    392, 392, 440, 392, 587, 523, 0,         /* Hap-py birth-day to you - pause */
    392, 392, 784, 659, 523, 494, 440, 0,    /* Hap-py birth-day dear ... - pause */
    698, 698, 659, 523, 587, 523 , -1        /* ... name - pause */
};
int16_t song3[] = {
    330, 330, 330, 0,                        /* Jin-gle bells - pause */
    330, 330, 330, 0,                        /* Jin-gle bells - pause */
    330, 392, 262, 294, 330, 0,              /* Jin-gle all the way - pause */
    349, 349, 349, 349, 349, 330, 330, 0,    /* Oh what fun it is to - pause */
    330, 330, 392, 392, 349, 294, 262, -1    /* ride in a one-horse sleigh */
};

int16_t *songs[] = {
    song1,
    song2,
    song3
};

static void play_buzzer(char *message);
static void translate_morse2alpha(char *morseMessage, char *alphaMessage, uint8_t messageLength);
static void play_song(uint8_t songIndex);

// IMU data reading task
static void data_task(void *arg){
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10000));

    ICM42670_start_with_default_values();
    // printf("ICM42670 initialized\n");

    float ax, ay, az, gx, gy, gz, t;
    bool first_run = true;

    for(;;){
        // Do stuff only if in READING state
        if (programState != READING){
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }


        tight_loop_contents();

        ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t);

        if(gx > 100){
            orientationState = HORIZONTAL;
        }

        if(gx < -100){
            orientationState = VERTICAL;
        }

        // Set initial orientation on first run
        if (first_run){
            orientationState = HORIZONTAL;
            first_run = false;
        }

        // printf("gx: %f, orientation: %d\n", gx, orientationState);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

//Task for sending message via USB
static void usb_sending_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); 

        // Do stuff only if a message is ready to send
        if(programState == READY_TO_SEND){
            programState = SENDING;

            // Add null terminator and send the message
            messageBuffer[messageCounter] = '\0';
            printf("Morse message: %s\n", messageBuffer);

            // Translate morse to alphabet and send
            char alphaMessage[MESSAGE_BUFFER_LENGTH];
            translate_morse2alpha(messageBuffer, alphaMessage, messageCounter);
            printf("Translated message: %s\n", alphaMessage);

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

// Task for receiving commands and messages via USB
static void usb_receiving_task(void *arg){
    (void)arg;

    // Buffer to store incoming commands and messages
    char rxBuffer[RX_COMMAND_BUFFER_LENGTH];
    uint16_t rxCounter = 0;

    for(;;){
        int c = getchar();

        if (c != EOF) {
            // Check for end of command/message
            if (c == '\n' || c == '\r') {
                rxBuffer[rxCounter] = '\0';

                // Process the received command/message
                if (rxCounter > 0 && programState == READING) {

                    programState = PROCESSING_MESSAGE;
                    
                    if (strcmp(rxBuffer, "/led") == 0) {
                        // Test LED by blinking 5 times
                        blink_led(5);
                        programState = READING;
                    }
                    else if (strcmp(rxBuffer, "/buzzer") == 0) {
                        // Test buzzer by playing a tone
                        buzzer_play_tone(500, 200);
                        programState = READING;
                    } 
                    else if (strcmp((rxBuffer), "/clear") == 0) {
                        // Clear the display
                        clear_display();    
                        programState = READING;
                    }
                    else if (strcmp((rxBuffer), "/play") == 0) {
                        // Enter the menu to select a song
                        currentMenuOption = 0;
                        programState = IN_MENU;
                    }
                    else{
                        // Write message to lcd screen
                        clear_display();

                        // Translate morse to alphabet and write to the display
                        char alphaMessage[MESSAGE_BUFFER_LENGTH];
                        translate_morse2alpha(rxBuffer, alphaMessage, rxCounter);
                        // printf("Translated message: %s\n", alphaMessage);
                        write_text(alphaMessage);

                        // Play the morse code message via buzzer
                        play_buzzer(rxBuffer);

                        programState = READING;
                    }
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

// Task for handling menu navigation and selection
static void menu_task(void *arg){
    (void)arg;
    uint8_t selectedMenuOption = -1;

    for(;;){
        tight_loop_contents();

        // Do stuff only if in IN_MENU state
        if(programState != IN_MENU){
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }

        // Update display if menu option has changed
        if(currentMenuOption != selectedMenuOption){
            selectedMenuOption = currentMenuOption;
            clear_display();
            write_text(menuOptions[currentMenuOption]);
        }

        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

// Task for handling button presses
static void button_task(void *arg){
    (void)arg;
    static uint8_t spaceCounter = 0;

    for(;;){
        tight_loop_contents();

        // Handle button presses based on current program state
        if (programState == READING){

            // Handle button presses for morse code input
            if(button1Pressed){
                // Add dot or dash based on orientation
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
                // Add space
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
        } else if (programState == IN_MENU){
            // Handle button presses for menu navigation

            if(button1Pressed){
                // Move to next menu option
                currentMenuOption = (currentMenuOption + 1) % (sizeof(menuOptions) / sizeof(menuOptions[0]));
                // printf("Current menu option: %s\n", menuOptions[currentMenuOption]);
                button1Pressed = false;
            }

            if(button2Pressed){
                // Select current menu option
                if (strcmp(menuOptions[currentMenuOption], "EXIT") == 0){
                    clear_display();
                    programState = READING;
                } else if (strcmp(menuOptions[currentMenuOption], "SELECT") != 0){
                    // printf("Playing %s\n", menuOptions[currentMenuOption]);

                    clear_display();
                    write_text("PLAYING");
                    
                    // Play song based on selection
                    play_song(currentMenuOption);

                    // After playing, return to menu
                    clear_display();
                    write_text(menuOptions[currentMenuOption]);
                }
                button2Pressed = false;
            }
        }

        button1Pressed = false;
        button2Pressed = false;
        
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

// GPIO interrupt callback for button presses
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

    // Morse code tree traversal
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

    // Add last letter and null-terminate the string
    alphaMessage[counter] = letter[index];
    alphaMessage[counter + 1] = '\0';
}

// Play buzzer based on morse code string
static void play_buzzer(char *string){
    
    char *ptr;

    for (ptr = string; *ptr != 0; ptr++) {
        if (*ptr == '-') {
            // Dash
            buzzer_play_tone(440, 300);
        } else if (*ptr == '.') {
            // Dot
            buzzer_play_tone(440, 100);
        } else {
            // Space
            vTaskDelay(pdMS_TO_TICKS(500));                    
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Play song based on menu selection
static void play_song(uint8_t songIndex){
    programState = PLAYING_SONG;

    // Validate song index
    if (songIndex < 1 || songIndex > sizeof(songs) / sizeof(songs[0])){
        programState = IN_MENU;
        return;
    }

    // printf("Playing song %d\n", songIndex) - 1;

    // Play notes until the ending note (-1) is reached
    for (size_t i = 0; songs[songIndex - 1][i + 1] >= 0; i++){
        int16_t frequency = songs[songIndex - 1][i];
        // printf("Playing frequency: %d\n", frequency);
        if (frequency == 0){
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            buzzer_play_tone(frequency, 250);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // printf("Finished playing song %d\n", songIndex) - 1;
    programState = IN_MENU;
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

    TaskHandle_t menuTaskHandle = NULL;
    BaseType_t menuTaskResult = xTaskCreate(menu_task,
        "menu_task",
        DEFAULT_STACK_SIZE,
        NULL,
        2,
        &menuTaskHandle);
    if(menuTaskResult != pdPASS) {
        printf("Menu task creation failed\n");
        return 0;
    }


    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, gpio_callback);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, gpio_callback);


    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}
