//ELEC 291 Project 2
//Receiver/Car controller
//Hannah Sawiuk, Adrian Viquez, Jake Osborne, Sajan Rajdev, Gregor Morrison

//********************************//
//	   	Included Libraries		  //	
//********************************//
#include <C8051F38x.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

//********************************//
//	   	Defined Constants		  //	
//********************************//
#define SYSCLK    48000000L // SYSCLK frequency in Hz
#define BAUDRATE  115200L   // Baud rate of UART in bps

//	ADC Pins	// 
#define backright_ADC 	LQFP32_MUX_P1_5
#define backleft_ADC 	LQFP32_MUX_P1_6
#define right_ADC 		LQFP32_MUX_P1_7
#define left_ADC    	LQFP32_MUX_P2_0
#define center_ADC  	LQFP32_MUX_P2_1

//	General Constants	//
#define pwm_constant	30 //Base PWM value
#define rotate_constant 45 //Rotate PWM value
#define Ithresh_voltage 1.5 //Approaching intersection voltage value
#define sensitivity 0.005 //maintain_d voltage sensitivity factor

//	MOSFET Pins	//
#define OUT_L1 P2_2
#define OUT_L0 P2_3
#define OUT_R1 P2_5
#define OUT_R0 P2_4

#define Receive P1_1 // 0 or 1: depends on instruction

//********************************//
//	   Function Prototypes	      //	
//********************************//
void maintain_d (void);
void stop (void);
void forward (void);
void backward (void);
void rotate(void);
void checkI_flag(void);
void turn_left(void);
void turn_right(void);
void bin2dec (void);

//********************************//
//	   	Volatile Variables		  //	note: type unsigned char or unsigned int only
//********************************//
		//	PWM	//
volatile unsigned char pwm_count = 0;
volatile unsigned char pwmr = 0;
volatile unsigned char pwml = 0;
volatile unsigned char direction = 1; //1=forward, 0 = backwards 
volatile unsigned char overflow_count = 0;
volatile unsigned char I_flag = 0; //1 = intersection detected, 0 = no intersection detected
volatile unsigned char pulse_count = 0;
volatile unsigned char decode_flag = 1;

		///	ADC	//
volatile float V_ADC[5];
volatile float dright = 0.0;
volatile float dleft = 0.0;

	//	Instructions //
volatile unsigned char action = 1;//stop
volatile unsigned char Instructions[3];

//********************************//
//	   	Main Initializations	  //	
//********************************//
char _c51_external_startup (void)
{
	PCA0MD&=(~0x40) ;    // DISABLE WDT: clear Watchdog Enable bit
	VDM0CN=0x80; // enable VDD monitor
	RSTSRC=0x02|0x04; // Enable reset on missing clock detector and VDD

	// CLKSEL&=0b_1111_1000; // Not needed because CLKSEL==0 after reset
	#if (SYSCLK == 12000000L)
		//CLKSEL|=0b_0000_0000;  // SYSCLK derived from the Internal High-Frequency Oscillator / 4 
	#elif (SYSCLK == 24000000L)
		CLKSEL|=0b_0000_0010; // SYSCLK derived from the Internal High-Frequency Oscillator / 2.
	#elif (SYSCLK == 48000000L)
		CLKSEL|=0b_0000_0011; // SYSCLK derived from the Internal High-Frequency Oscillator / 1.
	#else
		#error SYSCLK must be either 12000000L, 24000000L, or 48000000L
	#endif
	OSCICN |= 0x03; // Configure internal oscillator for its maximum frequency

	// Configure UART0
	SCON0 = 0x10; 
#if (SYSCLK/BAUDRATE/2L/256L < 1)
	TH1 = 0x10000-((SYSCLK/BAUDRATE)/2L);
	CKCON &= ~0x0B;                  // T1M = 1; SCA1:0 = xx
	CKCON |=  0x08;
#elif (SYSCLK/BAUDRATE/2L/256L < 4)
	TH1 = 0x10000-(SYSCLK/BAUDRATE/2L/4L);
	CKCON &= ~0x0B; // T1M = 0; SCA1:0 = 01                  
	CKCON |=  0x01;
#elif (SYSCLK/BAUDRATE/2L/256L < 12)
	TH1 = 0x10000-(SYSCLK/BAUDRATE/2L/12L);
	CKCON &= ~0x0B; // T1M = 0; SCA1:0 = 00
#else
	TH1 = 0x10000-(SYSCLK/BAUDRATE/2/48);
	CKCON &= ~0x0B; // T1M = 0; SCA1:0 = 10
	CKCON |=  0x02;
#endif
	//***	Timer1 for Communication	***//
	TL1 = TH1;      // Init Timer1
	TMOD &= ~0xf0;  // TMOD: timer 1 in 8-bit autoreload
	TMOD |=  0x20;                       
	TR1 = 1; // START Timer1
	TI = 1;  // Indicate TX0 ready
	
	// Configure the pins used for square output
	P2MDOUT|=0b_0000_1100;
	P0MDOUT |= 0x10; // Enable UTX as push-pull output
	XBR0     = 0x01; // Enable UART on P0.4(TX) and P0.5(RX)                     
	XBR1     = 0x40; // Enable crossbar and weak pull-ups

	// Initialize timer 2 for periodic interrupts
	//***	Timer2 for interrupts	***//
	TMR2CN=0x00;   // Stop Timer2; Clear TF2;
	CKCON|=0b_0001_0000;
	TMR2RL=(-(SYSCLK/(2*48))/(100L)); // Initialize reload value
	TMR2=0xffff;   // Set to reload immediately
	ET2=1;         // Enable Timer2 interrupts
	TR2=1;         // Start Timer2

	EA=1; // Enable interrupts
	
	return 0;
}

//********************************//
//	 	  WaitMilliSeconds	      //	
//********************************//
void Timer3us(unsigned char us)
{
	unsigned char i;               // usec counter
	
	// The input for Timer 3 is selected as SYSCLK by setting T3ML (bit 6) of CKCON:
	CKCON|=0b_0100_0000;
	
	TMR3RL = (-(SYSCLK)/1000000L); // Set Timer3 to overflow in 1us.
	TMR3 = TMR3RL;                 // Initialize Timer3 for first overflow
	
	TMR3CN = 0x04;                 // Sart Timer3 and clear overflow flag
	for (i = 0; i < us; i++)       // Count <us> overflows
	{
		while (!(TMR3CN & 0x80));  // Wait for overflow
		TMR3CN &= ~(0x80);         // Clear overflow indicator
		// Check overflow of Timer/Counter 0
		if (TF0==1)
		{
			TF0=0;
			overflow_count++;
		}
	}
	TMR3CN = 0 ;                   // Stop Timer3 and clear overflow flag
}

void waitms (unsigned int ms)
{
	unsigned int j;
	for(j=ms; j!=0; j--)
	{
		Timer3us(249);
		Timer3us(249);
		Timer3us(249);
		Timer3us(250);
	}
}

//********************************//
//	 	  Timer2 interrupt	  	  //	
//********************************//
void Timer2_ISR (void) interrupt 5
{
	TF2H = 0; // Clear Timer2 interrupt flag
	
	//printf("ISR\n");
	pwm_count++;
	if(pwm_count > 100) 
		pwm_count = 0;

	if(direction == 1)//forward
	{
		OUT_R0 = 0;
		OUT_R1 = pwm_count > (100-pwmr)?1:0;

		OUT_L0 = 0;
		OUT_L1 = pwm_count > (100-pwml)?1:0;
	}
	else if(direction == 0)//backward
	{		
		OUT_R0 = pwm_count > (100-pwmr)?1:0;
		OUT_R1 = 0;

		OUT_L0 = pwm_count > (100-pwml)?1:0;
		OUT_L1 = 0;
	}
	else if(direction == 2) 	//Left wheel forward, right wheel backwards
	{
		OUT_R0 = pwm_count > (100-pwmr)?1:0;
		OUT_R1 = 0;
		
		OUT_L0 = 0;
		OUT_L1 = pwm_count > (100-pwml)?1:0;
	}
	
	else 	//Right wheel forward, left wheel backwards
	{
		OUT_R0 = 0;
		OUT_R1 = pwm_count > (100-pwmr)?1:0;
		
		OUT_L0 = pwm_count > (100-pwml)?1:0;
		OUT_L1 = 0;
	}	
}
//********************************//
//	 	 Initialize ADC   		  //	
//********************************//
void InitADC (void)
{
	// Init ADC
	ADC0CF = 0xF8; // SAR clock = 31, Right-justified result
	ADC0CN = 0b_1000_0000; // AD0EN=1, AD0TM=0
  	REF0CN = 0b_0000_1000; //Select VDD as the voltage reference for the converter
}
//********************************//
//		 Initialize ADC Pins	  //	
//********************************//
void InitPinADC (unsigned char portno, unsigned char pinno)
{
	unsigned char mask;
	
	mask=1<<pinno;
	
	switch (portno)
	{
		case 0:
			P0MDIN &= (~mask); // Set pin as analog input
			P0SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		case 1:
			P1MDIN &= (~mask); // Set pin as analog input
			P1SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		case 2:
			P2MDIN &= (~mask); // Set pin as analog input
			P2SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		case 3:
			P3MDIN &= (~mask); // Set pin as analog input
			P3SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		default:
		break;
	}
}
//********************************//
//	 	 ADC Value at Pin  		  //	
//********************************//
unsigned int ADC_at_Pin(unsigned char pin)
{
	AMX0P = pin;             // Select positive input from pin
	AMX0N = LQFP32_MUX_GND;  // GND is negative input (Single-ended Mode)
	// Dummy conversion first to select new pin
	AD0BUSY=1;
	while (AD0BUSY); // Wait for dummy conversion to finish
	// Convert voltage at the pin
	AD0BUSY = 1;
	while (AD0BUSY); // Wait for conversion to complete
	return (ADC0L+(ADC0H*0x100));
}
//********************************//
//	 	 Voltage at Pin  		  //	
//********************************//
float Volts_at_Pin(unsigned char pin)
{
	 return ((ADC_at_Pin(pin)*3.30)/1024.0);
}
//********************************//
//	 	    Main Program   		  //	
//********************************//
void main (void)
{	
	//Configure pins as analog inputs
	InitPinADC(1,5); // Backright Inductor
	InitPinADC(1,6); // Backleft inductor
	InitPinADC(1,7); // Right inductor
	InitPinADC(2,0); // Left Inductor
	InitPinADC(2,1); // Center Inductor
	InitADC(); // Initialize the ADC
	
	while(1)
	{
		V_ADC[0]=Volts_at_Pin(right_ADC); //Right Inductor
		V_ADC[1]=Volts_at_Pin(left_ADC); //Left Inductor
		V_ADC[2]=Volts_at_Pin(center_ADC); //Center Inductor
		V_ADC[3]=Volts_at_Pin(backright_ADC); //Backright Inductor
		V_ADC[4]=Volts_at_Pin(backleft_ADC); //Backleft Inductor
		
		printf("VR=%f VL=%f VC=%f VRB=%f VLB=%f\r",V_ADC[0],V_ADC[1],V_ADC[2],V_ADC[3],V_ADC[4]);     

		//if (direction == 1 || direction == 0) //only maintain distance when going forward or backward
		//	maintain_d();

		//replace a with a value tbd, or make the car "search" for a voltage
		if(V_ADC[0] < 0.5 && V_ADC[1] < 0.5 && V_ADC[2] < 0.5) //if the car is off track, stop
			stop();
		else
			backward();
	} 
}

		//checkI_flag();


		/*switch(instruction)
		{
			case 1:
				stop();
				break;
			case 2:
				if(I_flag==1){
				turn_right();
				}
				break;
			case 3:
				if(I_flag==1){
				turn_left();
				}
				break;
			case 4:
				forward();
				break;
			case 5:
				backward();
				break;
			case 6:
				rotate();
				break;
			default:
				forward();
				break;
		}
*/
//}
//commands: stop, turn right at next intersection, turn left at next intersection, move forward, move backward, rotate 180 degrees
//also: manage distance

/*void instructions (void)
{
	//logic 1 = [1.7,]
	char pin=right;
	if(V[0] >= 1.7)
		pin = right;
	else if(V[1] >= 1.7)
		pin = left;
	else if(V[2] >= 1.7)
		pin = center
	else //if none of the voltages valid, off track, so stop
	{
		instruction = 1;//stop
		return;
	}

	TL0 = 0; 
	TH0 = 0;
	TF0 = 0;
	overflow_count = 0;

	while(pin != 0); // Wait for the signal to be zero
	while(pin != 1); // Wait for the signal to be one
	TR0 = 1; // Start the timer
	while(pin != 0) // Wait for the signal to be zero
	{
		if(TF0 == 1) // Did the 16-bit timer overflow?
		{
			TF0 = 0;
			overflow_count++;
		}
	}
	while(pin != 1) // Wait for the signal to be one
	{ 
		if(TF0 == 1) // Did the 16 -bit timer overflow?
		{
			TF0 = 0;
			overflow_count++;
		}
	}
	TR0 = 0; // Stop timer 0, the 24-bit number [overflow_count-TH0-TL0] has the period!
 	time = 1000*(overflow_count*65536.0+TH0*256.0+TL0)*(12.0/SYSCLK);

  //temp vars a and b tbd
  if (0.5 < time < 1.7)
  	instruction = 1;
  else if (a < time < b)
  	instruction = 2;
  else if (a < time < b)
  	instruction = 3;
  else if (a < time < b)
  	instruction = 4;
  else if (a < time < b)
  	instruction = 5;
  else if (a < time < b)
  	instruction = 6;
  else
  	instruction = 4;//forward
}
*/
//********************************//
//	 	Maintain Distance     	  //	
//********************************//
void maintain_d (void)
{
	//printf("maintain\n");
	if (direction == 1) //forwards
	{
		dright = V_ADC[0]; //right 
		dleft = V_ADC[1]; //left
	}
	else if (direction == 0)
	{
		dright = V_ADC[3]; //backleft (inverse logic)
		dleft = V_ADC[4]; //backright5
	}

	if (fabsf(dright-dleft) > sensitivity)//Constant adjusts sensitivity
	{
		if(dright > dleft)
		{
			pwmr = (unsigned char)(pwm_constant*fabsf(dleft/dright));
			pwml = pwm_constant;
		}
		else
		{
			pwml = (unsigned char)(pwm_constant*fabsf(dright/dleft));
			pwmr = pwm_constant;
		}
	}
	else
	{
		pwmr = pwm_constant;
		pwml = pwm_constant;
	}	
}

//********************************//
//	 	  	1:Stop    			  //	
//********************************//
void stop (void)
{
	pwml = 0;
	pwmr = 0;
}

//***************************************//
// 2:Turn right at the next intersection //	
//***************************************//

void turn_right(void)
{	
	//turn code
	//delay for short time
	//keep turning while checking voltages until fairly centered
	//then follow the wire
	
	direction = 3;
	pwmr = pwm_constant/2;
	pwml = pwm_constant;
	waitms(100);
	while(fabsf(V_ADC[0]-V_ADC[1]) > sensitivity)
	{
		V_ADC[0]=Volts_at_Pin(right_ADC); //Right Inductor -> negative input of the peak detector
		V_ADC[1]=Volts_at_Pin(left_ADC); //Left Inductor
	}
	forward(); //go forward after
}

//***************************************//
// 3:Turn left at the next intersection  //	
//***************************************//

void turn_left(void)
{	
	direction = 2;
	pwml = pwm_constant/2;
	pwmr = pwm_constant;
	waitms(100);
	while(fabsf(V_ADC[0]-V_ADC[1]) > sensitivity)
	{
		V_ADC[0]=Volts_at_Pin(right_ADC); //Right Inductor -> negative input of the peak detector
		V_ADC[1]=Volts_at_Pin(left_ADC); //Left Inductor
	}
	forward(); //go forward after

}

//********************************//
//	 	  4:Forward       //	
//********************************//
void forward(void)
{	
	//printf("forward\n");
	direction = 1;//forward
	pwml = pwm_constant;
	pwmr = pwm_constant;
	maintain_d();
}

//********************************//
//	 	  5:Backward      //	
//********************************//
void backward(void)
{	
	direction = 0;//backward
	pwml = pwm_constant;
	pwmr = pwm_constant;
	maintain_d();
}

//********************************//
//	 	  6:Rotate        		  //	
//********************************//
void rotate(void)
{
	direction = 2;
	pwml = rotate_constant; 
	pwmr = rotate_constant;
	waitms(100);
	while(fabsf(V_ADC[0]-V_ADC[1]) > sensitivity)
	{
		V_ADC[0]=Volts_at_Pin(right_ADC); //Right Inductor -> negative input of the peak detector
		V_ADC[1]=Volts_at_Pin(left_ADC); //Left Inductor
	}
	forward(); //go forward after	
}

//********************************//
//	 Check Intersection Flag      //
//********************************//
void checkI_flag(void)
{

	I_flag = 0; //Default. Not at intersection

	if(V_ADC[2] > Ithresh_voltage){
		//waitms(100);		 //Check if the voltage read by the third inductor
		I_flag = 1;             //is greater than the perpendicular voltage
	}
}