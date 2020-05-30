/*
 * MesTemp1.c
 *
 * Created: 2020/05/20 11:07:32
 * Author : kuras
 */ 
#define F_CPU 1000000

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <string.h>
#include "iic.h"

uint16_t  uiTimer_0 ;
// ���x����Ԋu(200ms�P��)
#define MES_INTERVAL 10
// ���x�ϊ�����(240ms�ȏ�)
#define CONVERSION_WAIT 1

// ����C���^�[�o�������A�����̕ϊ��I���҂����������t���O
uint8_t bMes_cycle;
/*
void Timer_init(void)
{
	// 8 bit Timer �ݒ肷��B
	//         ++--------------- COM0A �W���|�[�g����ɐݒ肷��B
	//         ||++------------- COM0B �W���|�[�g����ɐݒ肷��B
	//         ||||++----------- Reserved
	//         ||||||++--------- WGM0 CTC����
	//         ||||||||
	TCCR2A = 0b00000010 ;

	//         ++--------------- FOC0A, FOC0B
	//         ||++------------- Reserved
	//         ||||+------------ WGM02
	//         |||||+++--------- CS02, 1, 0 ( 1/1024 )
	//         ||||||||
	TCCR2B = 0b00000111 ;		// 1/1MHz * 1024 = 1024usec

	OCR2A = (194) ;		// Output Compare A Register 195 * 1024usec = 200msec

	//         +++++------------ Reserved
	//         |||||+----------- OCIE0B Output Compare Match B Interrupt Enable
	//         ||||||+---------- OCIE0A Output Compare Match A Interrupt Enable
	//         |||||||+--------- TOIE0  Timer Overflow Interrupt Enable
	//         ||||||||
	TIMSK2 = 0b00000010 ;
	
	uiTimer_0 = 15;
}
*/
void setup_WDT( uint8_t delay )
{
	if( delay > 9 ) delay = 9;
	uint8_t reg_data = delay & 7;
	if( delay & 0x8 )
	{
		reg_data |= 0x20;
	}
	reg_data |= ( 1 << WDCE );

	MCUSR &= ~(1 << WDRF);
	WDTCSR |= (1 << WDCE) | (1 << WDE);   // �E�H�b�`�h�b�O�ύX���iWDCE��4�T�C�N���Ŏ������Z�b�g�j
	// set new watchdog timeout value
	WDTCSR = reg_data;                          // ���䃌�W�X�^��ݒ�
	WDTCSR |= (1 << WDIE);
}

//#define BAUD_PRESCALE 51 // U2X=0 8MHz 9600B
//#define BAUD_PRESCALE 12 // U2X=0 2MHz 9600B
#define BAUD_PRESCALE 12 // U2X=1 1MHz 9600B

uint8_t bRecieve;
char *waiting_message;

void setup_USART(void){
	// Set baud rate
	UBRR0L = BAUD_PRESCALE;// Load lower 8-bits into the low byte of the UBRR register
	UBRR0H = (BAUD_PRESCALE >> 8);

	// �{���r�b�gON
	UCSR0A = 2;
	// Enable receiver and transmitter and receive complete interrupt
	UCSR0B = ((1<<TXEN0)|(1<<RXEN0) | (1<<RXCIE0));
	UCSR0C = 0b00000110;
}

void USART_SendByte(uint8_t u8Data){

	// Wait until last byte has been transmitted
	while((UCSR0A &(1<<UDRE0)) == 0);

	// Transmit data
	UDR0 = u8Data;
	
	// clear tx complete flag
	UCSR0A |= (1<<TXC0);
}

void USART_SendStr(char * str)
{
	uint8_t i = 0;
	while( str[i] != '\0' )
	{
		USART_SendByte(str[i++]);
	}
	
	// wait for tx complete
	while( (UCSR0A & (1<<TXC0)) == 0 );
}

void set_BLE_sleep()
{
	_delay_ms(10);	// �K�v�B�傫������
	PORTC &= (~2);	// set C1 Low
	DDRC |= 2;		// set C1 output
	_delay_ms(500);	// �K�v�B�傫������
	
	bRecieve = 0;
	waiting_message = "OK";
	USART_SendStr("AT+SLEEP1\r\n");
	//_delay_ms(500);	// �K�v�B�傫������
	set_sleep_mode(SLEEP_MODE_IDLE);
	while(bRecieve == 0)
	{
		sleep_mode();
	}
	DDRC &= (~2);	// set C1(PWRC) Hi-Z
}

void BLE_send_message(char *str)
{
	// BLE��SLEEP��������
	PORTC &= (~2);
	DDRC |= 2;
	_delay_ms(10);
	DDRC &= (~2);
	_delay_ms(10);

	USART_SendStr(str);

	set_BLE_sleep();
}

#define RCV_SIZE 100
char RCV_BUF[RCV_SIZE];
uint8_t RCV_PTR = 0;

void check_command()
{
	if( strstr(RCV_BUF, waiting_message) != NULL )
	{
		bRecieve = 1;
	}
}

ISR (USART_RX_vect)
{
	char value = UDR0;             //read UART register into value
	if( value == '\x0d' )
	{
	}
	else if( value == '\x0a' )
	{
		RCV_BUF[RCV_PTR] = '\0';
		check_command();
		RCV_PTR = 0;
	}
	else
	{
		RCV_BUF[RCV_PTR] = value;
		if( RCV_PTR < RCV_SIZE-1 )
		{
			RCV_PTR++;
		}
	}
}

int write_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
	iic_start();
	int ack = iic_send(dev_addr<<1);
	if( ack != 0 )
	{
		return 10;
	}

	ack = iic_send(reg_addr);
	if( ack != 0 )
	{
		return 11;
	}

	ack = iic_send(data);
	if( ack != 0 )
	{
		return 12;
	}

	iic_stop();
	return 0;
}

int set_one_shot(uint8_t dev_addr)
{
	// one-shot mode
	int ack = write_reg(dev_addr, 0x03, 0x40);
	if( ack != 0 )
	{
		return ack; // ack error
	}
	// wait for temperature measure and conversion (typical:240ms)
	//_delay_ms(300);
	return 0;
}

int read_temp( uint8_t dev_addr, int *value )
{
	int ack;

	// read temperature value (r0,r1)
	iic_start();
	ack = iic_send(dev_addr<<1);
	if( ack != 0 )
	{
		return 1; // ack error
	}

	ack = iic_send(0x0);
	if( ack != 0 )
	{
		return 2; // ack error
	}
	
	iic_start();

	ack = iic_send(dev_addr<<1 | 1);
	if( ack != 0 )
	{
		return 3; // ack error
	}

	unsigned char id0 = iic_recv(0);
	unsigned char id1 = iic_recv(1);
	iic_stop();
	*value = id0<<8 | id1;
	return 0;
}

void temp_to_string( int temp, char *buf )
{
	uint32_t under_0 = 0; // �����_�ȉ����v�Z����
	uint32_t exp_minus_4 = 625; // 0.0625�̂���
	for( int i=3; i<7; i++ ) // ��������bit6-bit3
	{
		if( temp & (1<<i) )
		under_0 += exp_minus_4;
		exp_minus_4 *= 2;
	}
	under_0 += 500; // �������ʂŎl�̌ܓ�
	under_0 /= 1000; // �������ʂ������c��

	sprintf( buf, "%d.%01ld", temp>>7, under_0 );
}

void meas()
{
	uiTimer_0--;
	if( uiTimer_0 == 0 )
	{
		char buf[100];
		if( bMes_cycle == 0 )
		{
			int err = set_one_shot(0x48);
			if( err != 0 )
			{
				sprintf( buf, "ERROR=%d\r\n", err );
				BLE_send_message( buf );
				uiTimer_0 = MES_INTERVAL;
			}
			else
			{
				bMes_cycle = 1;
				uiTimer_0 = CONVERSION_WAIT;
			}
		}
		else
		{
			uiTimer_0 = MES_INTERVAL;
			bMes_cycle = 0;

			int tmp_i;
			int err = read_temp(0x48, &tmp_i);
			if( err != 0 )
			{
				sprintf( buf, "ERROR=%d\r\n", err );
			}
			else
			{
				char temp_buf[10];
				temp_to_string( tmp_i, temp_buf );
				sprintf(buf, "Temp=%s\r\n", temp_buf);
			}
			BLE_send_message( buf );
		}
	}
}
//---------------------------------------------------------------------------
//	Timer ( 8bit ) ������ (CTC ���[�h)
//---------------------------------------------------------------------------
// 8 msec ���Ɋ����݂���������B
/*
ISR (TIMER2_COMPA_vect)
{
}
*/
ISR (WDT_vect)
{
}

int main(void)
{
	//cli();			// �����݂��֎~����B
	CLKPR = 0x80;// �N���b�N������ύX���r�b�gON
	CLKPR = 0x03;// ������1/8 : �N���b�N��1MHz

	setup_USART();
	DDRC |= 1;	// set C0(LED) output

	_delay_ms(1000*2);
	sei();			// �����݂�������B
	set_BLE_sleep();

	setup_iic();
	write_reg(0x48, 0x3, 0x60); // set shutdown mode (13bit mode)

	//Timer_init();
	bMes_cycle = 0;

	uiTimer_0 = 1;
	while (1)
	{
		setup_WDT(9);
		set_sleep_mode(SLEEP_MODE_PWR_DOWN);
		sleep_mode();
		meas();
	}
}
