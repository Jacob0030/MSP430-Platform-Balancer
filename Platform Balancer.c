// A platform Balancing Control.

#include <msp430fr6989.h>
#include <stdint.h>

#define BUT1 BIT1 // Button S1 at Port 1.1
#define BUZZ BIT7 // Buzzer at Port 2.7
#define FLAGS UCA1IFG // Contains the transmit & receive flags
#define RXFLAG UCRXIFG // Receive flag
#define TXFLAG UCTXIFG // Transmit flag
#define TXBUFFER UCA1TXBUF // Transmit buffer
#define RXBUFFER UCA1RXBUF // Receive buffer

// Function Prototypes
void Initialize_UART(void);
void Initialize_ADC();
void uart_write_char(unsigned char ch);
void uart_write_string (char * str);
void uart_write_uint16 (unsigned int n);
unsigned char uart_read_char(void);
void platform_balancing_interface(void);
void move_cursor (void);
void adjust_motors(void);
void calculate_delta(void);
void danger(void);

// array for the motor values
unsigned int motor_values[4] = {127, 127, 127, 127};

// variables
volatile unsigned int mode = 0;
volatile unsigned int result_x = 0;
volatile unsigned int result_y = 0;
volatile unsigned int cursor = 1;
volatile unsigned int maximum;
volatile unsigned int minimum;
volatile unsigned int delta;

void main(void) {

    WDTCTL = WDTPW | WDTHOLD;   // Stop WDT
    PM5CTL0 &=  ~LOCKLPM5;      // Enable GPIO pins

    // Configure button S1
    P1DIR &=  ~BUT1;            // Direct pin as input
    P1REN |= BUT1;               // Enable built-in resistor
    P1OUT |= BUT1;               // Set resistor as pull-up

    // Configure buzzer
    P2DIR |= BUZZ;
    P2OUT &= ~BUZZ;

    //  Initialize UART and ADC
    Initialize_UART();
    Initialize_ADC();

    platform_balancing_interface();
    uart_write_string("\033[16;2H");
    uart_write_string("*");
    uart_write_string("\033[6;10H");

    for (;;){
        if (mode == 0){
            move_cursor();
            }

        if (mode == 1){
            adjust_motors();
            calculate_delta();
            danger();
            }

        if (((P1IN & BUT1) == 0) && (mode == 0)){
            uart_write_string("\033[16;2H");
            uart_write_string(" ");
            uart_write_string("\033[17;2H");
            uart_write_string("*");
            mode = 1;
            _delay_cycles(500000);
        }

        else if (((P1IN & BUT1) == 0) && (mode == 1)){
            uart_write_string("\033[17;2H");
            uart_write_string(" ");
            uart_write_string("\033[16;2H");
            uart_write_string("*");
            mode = 0;
            uart_write_string("\033[6;10H");
            _delay_cycles(500000);
        }

    }
   }


// Configure UART to the popular configuration
// 9600 baud, 8-bit data, LSB first, no parity bits, 1 stop bit
// no flow control, oversampling reception
// Clock: SMCLK @ 1 MHz (1,000,000 Hz)
void Initialize_UART(void){
    // Configure pins to UART functionality
    P3SEL1 &=  ~(BIT4|BIT5);
    P3SEL0 |= (BIT4|BIT5);

    // Main configuration register
    UCA1CTLW0 = UCSWRST; // Engage reset; change all the fields to zero
    // Most fields in this register, when set to zero, correspond to the
    // popular configuration
    UCA1CTLW0 |= UCSSEL_2; // Set clock to SMCLK

    // Configure the clock dividers and modulators (and enable oversampling)
    UCA1BRW = 6; // divider
    // Modulators: UCBRF = 8 = 1000 --> UCBRF3 (bit #3)
    // UCBRS = 0x20 = 0010 0000 = UCBRS5 (bit #5)
    UCA1MCTLW = UCBRF3 | UCBRS5 | UCOS16;

    // Exit the reset state
    UCA1CTLW0 &=  ~UCSWRST;
    }

void Initialize_ADC() {
    // Divert the pins to analog functionality
    // X-axis: A10/P9.2, for A10 (P9DIR=x, P9SEL1=1, P9SEL0=1)
    // Y-axis: A4/P8.7, for A4 (P8DIR, P8SEL1=1, P8SEL0=1)
    P9SEL1 |= BIT2;
    P9SEL0 |= BIT2;
    P8SEL1 |= BIT7;
    P8SEL0 |= BIT7;

    // Turn on the ADC module
    ADC12CTL0 |= ADC12ON;

    // Turn off ENC (Enable Conversion) bit while modifying the configuration
    ADC12CTL0 &=  ~ADC12ENC;

    //*************** ADC12CTL0 ***************
    // Set ADC12SHT0 (select the number of cycles that you determined)
    // Set the bit ADC12MSC (Multiple Sample and Conversion)
    ADC12CTL0 |= (ADC12SHT0_3|ADC12MSC);

    //*************** ADC12CTL1 ***************
    // Set ADC12SHS (select ADC12SC bit as the trigger)
    // Set ADC12SHP bit
    // Set ADC12DIV (select the divider you determined)
    // Set ADC12SSEL (select MODOSC)
    // Set ADC12CONSEQ (select sequence-of-channels)
    ADC12CTL1 |= (ADC12SHP|ADC12CONSEQ_1);

    //*************** ADC12CTL2 ***************
    // Set ADC12RES (select 12-bit resolution)
    // Set ADC12DF (select unsigned binary format)
    ADC12CTL2 |= ADC12RES_2;

    //*************** ADC12CTL3 ***************
    // Set ADC12CSTARTADD to 0 (first conversion in ADC12MEM0)
    ADC12CTL3 &= ~ADC12CSTARTADD_31;

    //*************** ADC12MCTL0 ***************
    // Set ADC12VRSEL (select VR+=AVCC, VR-=AVSS)
    // Set ADC12INCH (select channel A10)
    ADC12MCTL0 |= (ADC12INCH_10);

    //*************** ADC12MCTL1 ***************
    // Set ADC12VRSEL (select VR+=AVCC, VR-=AVSS)
    // Set ADC12INCH (select the analog channel that you found)
    // Set ADC12EOS (last conversion in ADC12MEM1)
    ADC12MCTL1 |= (ADC12INCH_4|ADC12EOS);

    // Turn on ENC (Enable Conversion) bit at the end of the configuration
    ADC12CTL0 |= ADC12ENC;

    return;
}


// Writes a char with UART
void uart_write_char(unsigned char ch){
    // Wait for any ongoing transmission to complete
    while ( (FLAGS & TXFLAG)==0 ) {}

    // Copy the byte to the transmit buffer
    TXBUFFER = ch; // Tx flag goes to 0 and Tx begins!

    return;
}

// Function that writes a string over UART
void uart_write_string (char * str){

    //declare pointer for string
    char * ptr;
    ptr = str;

    // While string pointer is not pointing to a null char, write
    while (*ptr != '\0'){
    uart_write_char (*ptr);
    ptr++;
    }

    return;
    }


// Function that transmits a byte over UART
void uart_write_uint16 (unsigned int n){

    int digit;

    //  Parse Digits
    if (n > 9999){
        digit = n/10000;
        uart_write_char('0'+ digit);
    }

    if (n > 999){
        digit = (n/1000)%10;
        uart_write_char('0'+ digit);
    }

    if (n > 99){
        digit = (n/100)%10;
        uart_write_char('0'+ digit);
    }

    if (n > 9){
        digit = (n/10)%10;
        uart_write_char('0'+ digit);
    }

    digit = n%10;
    uart_write_char ('0' + digit);

    return;
    }


// Function returns the byte; if none received, returns null character
unsigned char uart_read_char(void){
    unsigned char temp;

    // Return null character (ASCII=0) if no byte was received
    if( (FLAGS & RXFLAG) == 0)
        return 0;

    // Otherwise, copy the received byte (this clears the flag) and return it
    temp = RXBUFFER;
    return temp;
    }

void platform_balancing_interface(void){
    uart_write_string("\033[2J");
    uart_write_string("\033[1;1H");
    uart_write_string ("*** PLATFORM BALANCING CONTROL ***");
    uart_write_string("\033[4;16H");
    uart_write_string("TOP");
    uart_write_string("\033[5;8H");
    uart_write_string("....................");
    uart_write_string("\033[6;8H");
    uart_write_string("|");
    uart_write_string("\033[6;10H");
    uart_write_uint16(motor_values[0]);
    uart_write_string("\033[6;23H");
    uart_write_uint16(motor_values[1]);
    uart_write_string("\033[6;27H");
    uart_write_string("|");
    uart_write_string("\033[7;6H");
    uart_write_string("L |");
    uart_write_string("\033[7;27H");
    uart_write_string("| R");
    uart_write_string("\033[8;6H");
    uart_write_string("E |");
    uart_write_string("\033[8;27H");
    uart_write_string("| I");
    uart_write_string("\033[9;6H");
    uart_write_string("F |");
    uart_write_string("\033[9;27H");
    uart_write_string("| G");
    uart_write_string("\033[10;6H");
    uart_write_string("T |");
    uart_write_string("\033[10;27H");
    uart_write_string("| H");
    uart_write_string("\033[11;8H");
    uart_write_string("|");
    uart_write_string("\033[11;27H");
    uart_write_string("| T");
    uart_write_string("\033[12;8H");
    uart_write_string("|");
    uart_write_string("\033[12;10H");
    uart_write_uint16(motor_values[2]);
    uart_write_string("\033[12;27H");
    uart_write_string("|");
    uart_write_string("\033[12;23H");
    uart_write_uint16(motor_values[3]);
    uart_write_string("\033[13;8H");
    uart_write_string("....................");
    uart_write_string("\033[14;15H");
    uart_write_string("BOTTOM");
    uart_write_string("\033[16;4H");
    uart_write_string("MOVE CURSOR");
    uart_write_string("\033[16;20H");
    uart_write_string("MAX DELTA");
    uart_write_string("\033[17;4H");
    uart_write_string("ADJUST VALUE");
    uart_write_string("\033[17;20H");
    uart_write_uint16(delta);
    uart_write_string("\033[17;26H");
    uart_write_string("mm");

    return;
}

void move_cursor (void){

    ADC12CTL0 |= ADC12SC;
    while ((ADC12CTL1 & ADC12BUSY) != 0) {}

    result_x = ADC12MEM0;
    result_y = ADC12MEM1;

    // Move Cursor from position 1 (top left)
    if ((result_x > 3000) && (cursor == 1)){
        uart_write_string("\033[6;23H");
        cursor = 2;
        }

    if ((result_y < 100) && (cursor == 1)){
        uart_write_string("\033[12;10H");
        cursor = 3;
        }

    if ((result_y < 100) && (result_x > 3000) && (cursor == 1)){
        uart_write_string("\033[12;23H");
        cursor = 4;
        }

    // Move Cursor from position 2 (top right)
    if ((result_x < 100) && (cursor == 2)){
        uart_write_string("\033[6;10H");
        cursor = 1;
        }

   if ((result_y < 100) && (cursor == 2)){
        uart_write_string("\033[12;23H");
        cursor = 4;
        }

   if ((result_y < 100) && (result_x < 100) && (cursor == 2)){
       uart_write_string("\033[12;10H");
       cursor = 3;
       }

   // Move Cursor from position 3 (bottom left)
   if ((result_x > 3000) && (cursor == 3)){
       uart_write_string("\033[12;23H");
       cursor = 4;
       }

   if ((result_y > 3000) && (cursor == 3)){
       uart_write_string("\033[6;10H");
       cursor = 1;
       }

   if ((result_y > 3000) && (result_x > 3000) && (cursor == 3)){
       uart_write_string("\033[6;23H");
       cursor = 2;
       }

   // Move Cursor from position 4 (bottom right)
   if ((result_x < 100) && (cursor == 4)){
       uart_write_string("\033[12;10H");
       cursor = 3;
       }

   if ((result_y > 3000) && (cursor == 4)){
       uart_write_string("\033[6;23H");
       cursor = 2;
       }

   if ((result_y > 3000) && (result_x < 100) && (cursor == 4)){
       uart_write_string("\033[6;10H");
       cursor = 1;
       }

    return;
}


void adjust_motors(void){

    ADC12CTL0 |= ADC12SC;
    while ((ADC12CTL1 & ADC12BUSY) != 0) {}

    result_x = ADC12MEM0;
    result_y = ADC12MEM1;

    if (cursor == 1){
        uart_write_string("\033[6;10H");
        if ((result_y > 3000) && (motor_values[0] != 256)){
            uart_write_uint16(motor_values[0]++);
        }

        if (result_y < 100){
            motor_values[0]--;

            if (motor_values[0] > 100){
                uart_write_uint16(motor_values[0]);
                }

            if (motor_values[0] < 100){
                uart_write_string("0");
                uart_write_uint16(motor_values[0]);
                }

            if (motor_values[0] < 10){
                uart_write_string("00");
                uart_write_uint16(motor_values[0]);
                }
        }
    }

    if (cursor == 2){
        uart_write_string("\033[6;23H");
        if ((result_y > 3000) && (motor_values[1] != 256)){
            uart_write_uint16(motor_values[1]++);
        }

        if (result_y < 100){
            motor_values[1]--;

            if (motor_values[1] > 100){
                uart_write_uint16(motor_values[1]);
                }

            if (motor_values[1] < 100){
                uart_write_string("0");
                uart_write_uint16(motor_values[1]);
                }

            if (motor_values[1] < 10){
                uart_write_string("00");
                uart_write_uint16(motor_values[1]);
                };
        }
    }

    if (cursor == 3){
        uart_write_string("\033[12;10H");
        if ((result_y > 3000) && (motor_values[2] != 256)){
            uart_write_uint16(motor_values[2]++);
        }

        if (result_y < 100){

            motor_values[2]--;

            if (motor_values[2] > 100){
                uart_write_uint16(motor_values[0]);
                }

            if (motor_values[2] < 100){
                uart_write_string("0");
                uart_write_uint16(motor_values[0]);
                }

            if (motor_values[2] < 10){
                uart_write_string("00");
                uart_write_uint16(motor_values[0]);
                }
        }
        }

    if (cursor == 4){
        uart_write_string("\033[12;23H");
        if ((result_y > 3000) && (motor_values[3] != 256)){
            uart_write_uint16(motor_values[3]++);
        }

        if (result_y < 100){
            motor_values[3]--;

            if (motor_values[3] > 100){
                uart_write_uint16(motor_values[0]);
                }

            if (motor_values[3] < 100){
                uart_write_string("0");
                uart_write_uint16(motor_values[0]);
                }

            if (motor_values[3] < 10){
                uart_write_string("00");
                uart_write_uint16(motor_values[0]);
                }
        }
    }

    _delay_cycles(100000);

    return;
    }


void calculate_delta(void){

    int i;
    int j;
    maximum = motor_values[0];
    minimum = motor_values[0];

    for (i=0; i<4; i++){
        if (motor_values[i] > maximum){
            maximum = motor_values[i];
        }
    }
    uart_write_string("\033[20;24H");

    for (j=0; j<4; j++){
        if (motor_values[j] < minimum){
            minimum = motor_values[j];
        }
    }
    uart_write_string("\033[21;24H");

    delta = maximum - minimum;
    uart_write_string("\033[17;20H");
    uart_write_uint16(delta);
    uart_write_string("   ");
    }

void danger(void){
    int i;

    if (delta > 10){
        for (i = 60,000; i> 0; i =(i-2*5000)){
            P2OUT |= BIT7;
            _delay_cycles(5000);
            P2OUT &= ~BIT7;
            _delay_cycles(5000);
        }
    }

    if (delta <= 10){
        P2OUT &= ~BIT7;
    }
}
