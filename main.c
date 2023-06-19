#define F_CPU 16000000
#include <avr/io.h>
#include <util/delay.h>

#include "UART.h"
#include <stdlib.h>

#define PORT_DATA		PORTD		// 데이터 핀 연결 포트
#define PORT_CONTROL	PORTB		// 제어 핀 연결 포트
#define DDR_DATA		DDRD		// 데이터 핀의 데이터 방향
#define DDR_CONTROL		DDRB		// 제어 핀의 데이터 방향

#define RS_PIN			0		// RS 제어 핀의 비트 번호
#define E_PIN			1		// E 제어 핀의 비트 번호

#define COMMAND_CLEAR_DISPLAY	0x01
#define COMMAND_8_BIT_MODE		0x38	// 8비트, 2라인, 5x8 폰트
#define COMMAND_4_BIT_MODE		0x28	// 4비트, 2라인, 5x8 폰트

#define COMMAND_DISPLAY_ON_OFF_BIT		2
#define COMMAND_CURSOR_ON_OFF_BIT		1
#define COMMAND_BLINK_ON_OFF_BIT		0
#define PRESCALER 1024 // 분주비


#define ROTATION_DELAY1000  1000 
#define ROTATION_DELAY1200  1200
#define ROTATION_DELAY1400  1400
#define ROTATION_DELAY1600  1600  

//#define PULSE_MIN		(40000*0.0325)	// 최소 펄스 길이: 1300
#define PULSE_MIN		(40000*0.0325)	// 최소 펄스 길이: 1580
#define PULSE_MAX		(40000*0.1295)	// 최대 펄스 길이: 4700
//#define PULSE_MAX		(40000*0.1355)	// 최대 펄스 길이: 5460
//#define PULSE_MID		((PULSE_MAX + PULSE_MIN)/2) // 중간 펄스 길이: 3000
#define PULSE_MID		((PULSE_MAX + PULSE_MIN)/2 + 250) // 중간 펄스 길이: 3770


#include "OpenWeather.h"

uint8_t MODE = 4;

void LCD_pulse_enable(void) {       // 하강 에지에서 동작
	PORT_CONTROL |= (1 << E_PIN);    // E를 HIGH로
	_delay_us(1);
	PORT_CONTROL &= ~(1 << E_PIN);   // E를 LOW로
	_delay_ms(1);
}

void LCD_write_data(uint8_t data)
{
	PORT_CONTROL |= (1 << RS_PIN);    // 문자 출력에서 RS는 1
	if(MODE == 8){
		PORT_DATA = data;             // 출력할 문자 데이터
		LCD_pulse_enable();           // 문자 출력
	}
	else{
		PORT_DATA = data & 0xF0;      // 상위 4비트
		LCD_pulse_enable();
		PORT_DATA = (data << 4) & 0xF0;    // 하위 4비트
		LCD_pulse_enable();
	}
}

void LCD_write_command(uint8_t command)
{
	PORT_CONTROL &= ~(1 << RS_PIN);    // 명령어 실행에서 RS는 0
	
	if(MODE == 8){
		PORT_DATA = command;        // 데이터 핀에 명령어 전달
		LCD_pulse_enable();            // 명령어 실행
	}
	else{
		PORT_DATA = command & 0xF0;    // 상위 4비트
		LCD_pulse_enable();
		PORT_DATA = (command << 4) & 0xF0;    // 하위 4비트
		LCD_pulse_enable();
	}
}

void LCD_clear(void)
{
	LCD_write_command(COMMAND_CLEAR_DISPLAY);
	_delay_ms(2);
}

void LCD_init(void)
{
	_delay_ms(50);
	
	// 연결 핀을 출력으로 설정
	if(MODE == 8) DDR_DATA |= 0xFF;
	else DDR_DATA |= 0xF0;
	
	DDR_CONTROL |= (1 << RS_PIN) | (1 << E_PIN);

	if(MODE == 8)
	LCD_write_command(COMMAND_8_BIT_MODE);        // 8비트 모드
	else{
		LCD_write_command(0x02);                  // 4비트 모드 추가 명령
		LCD_write_command(COMMAND_4_BIT_MODE);    // 4비트 모드
	}

	// display on/off control
	// 화면 on, 커서 off, 커서 깜빡임 off
	uint8_t command = 0x08 | (1 << COMMAND_DISPLAY_ON_OFF_BIT);
	LCD_write_command(command);

	LCD_clear();    // 화면 지움

	// Entry Mode Set
	// 출력 후 커서를 오른쪽으로 옮김, 즉, DDRAM의 주소가 증가하며 화면 이동은 없음
	LCD_write_command(0x06);
}

void LCD_write_string(char *string)
{
	uint8_t i;
	for(i = 0; string[i]; i++)            // 종료 문자를 만날 때까지
	LCD_write_data(string[i]);             // 문자 단위 출력
}

void LCD_goto_XY(uint8_t row, uint8_t col)
{
	col %= 16;        // [0 15]
	row %= 2;         // [0 1]

	// 첫째 라인 시작 주소는 0x00, 둘째 라인 시작 주소는 0x40
	uint8_t address = (0x40 * row) + col;
	uint8_t command = 0x80 + address;
	
	LCD_write_command(command);    // 커서 이동
}


void ADC_INIT(unsigned char channel)
{
	ADMUX |= 0x40;             // AVCC를 기준 전압으로 선택
	
	ADCSRA |= 0x07;            // 분주비 설정
	ADCSRA |= (1 << ADEN);    // ADC 활성화
	ADCSRA |= (1 << ADATE);   // 자동 트리거 모드

	ADMUX |= ((ADMUX & 0xE0) | channel);    // 채널 선택
	ADCSRA |= (1 << ADSC);    // 변환 시작
}

int read_ADC(void)
{
	while(!(ADCSRA & (1 << ADIF)));   // 변환 종료 대기
	return ADC;                		// 10비트 값을 반환
}



void Timer_init(void){
	TCCR0B |= (1 << CS02) | (1 << CS00); // 분주비 1024로 설정
}

uint8_t measure_distance(void)
{
	// 트리거 핀으로 펄스 출력
	PORTC |= (1 << PC0); // HIGH 값 출력
	_delay_us(10); // 10마이크로초 대기
	PORTC &= ~(1 << PC0); // LOW 값 출력

	// 에코 핀이 HIGH가 될 때까지 대기
	TCNT0 = 0;
	while(!(PINC & 0x02))
	if(TCNT0 > 250) return 255; // 장애물이 없는 경우

	// 에코 핀이 LOW가 될 때까지의 시간 측정
	TCNT0 = 0; // 카운터를 0으로 초기화
	while(PINC & 0x02){
		if (TCNT0 > 250){ // 측정 불가능
			TCNT0 = 0;
			break;
		}
	}

	// 에코 핀의 펄스폭을 마이크로초 단위로 계산
	double pulse_width = TCNT0 * PRESCALER * 1000000.0 / F_CPU;

	return pulse_width / 58; // 센티미터 단위 거리 반환
}

void INIT_TIMER1(void)
{
	// Fast PWM 모드, TOP = ICR1
	TCCR1A |= (1 << WGM11);
	TCCR1B |= (1 << WGM12) | (1 << WGM13);

	TCCR1B |= (1 << CS11);		// 분주율 8, 2MHz
	
	ICR1 = 40000;				// 20ms 주기
	
	TCCR1A |= (1 << COM1B1);		// 비반전 모드
	
	//  OC1B (PB2, 아두이노 10번) 핀을 출력으로 설정
	DDRB |= (1 << PB2);
}


int main(void)
{
	int read;
	float input_voltage, temperature;
	
	UART_INIT();

	
	uint8_t distance;
	DDRC |= 0x01; // 트리거 핀 출력으로 설정
	//    DDRC &= ~0xFD; // 에코 핀 입력으로 설정
	DDRC &= 0xFD; // 에코 핀 입력으로 설정
	UART_INIT(); // UART 통신 초기화
	Timer_init(); // 타이머 초기화

	while(1)
	{
		distance = measure_distance(); // 거리 측정

		_delay_ms(1000);
	
		if (distance < 8)
		{
			break;
		}
	}
	   
	

	ADC_INIT(2);
	
	DDRB = 0x08;			// PB3 연결된 LED 출력
	PORTB = 0x00;
	
	LCD_init();					
	LCD_write_string("Welcome!");	
	_delay_ms(2000);		
	LCD_clear();				
	
	LCD_write_string("Thank you for");	
	LCD_goto_XY(1, 0);
	LCD_write_string("your hard work!");	
	_delay_ms(2000);			
	
	LCD_clear();				
	
	
	
	INIT_TIMER1();
	
	
	while (1) {
		read = read_ADC();
		
		input_voltage = read * 5.0 / 1023.0;
		temperature = (input_voltage - 0.5) * 100.0;
		
		char buffer[65] = {0};
			
		LCD_write_string("Int. temp. : ");	
		LCD_goto_XY(0, 12);
		LCD_write_string(itoa((uint32_t)temperature,buffer, 10));
		LCD_write_string("`C");

		
		LCD_goto_XY(1, 0);
		LCD_write_string("Out. temp. : ");
		LCD_goto_XY(1, 12);
		LCD_write_string("`C");
		
		_delay_ms(100);						// 0.5초 마다 출력
		
		
		if (temperature >= 35)
		{
			OCR1B = PULSE_MIN;		// 0도
			_delay_ms(ROTATION_DELAY1000);
			OCR1B = PULSE_MAX;		// 180도
			_delay_ms(ROTATION_DELAY1000);
		}	
		
		else if (temperature >= 30)
		{
			OCR1B = PULSE_MIN;		// 0도
			_delay_ms(ROTATION_DELAY1200);
			OCR1B = PULSE_MAX;		// 180도
			_delay_ms(ROTATION_DELAY1200);
	
		}
		
		else if (temperature >= 25)
		{
			OCR1B = PULSE_MIN;		// 0도
			_delay_ms(ROTATION_DELAY1400);
			OCR1B = PULSE_MAX;		// 180도
			_delay_ms(ROTATION_DELAY1400);
		}
		
		else if (temperature >= 20)
		{
			OCR1B = PULSE_MIN;		// 0도
			_delay_ms(ROTATION_DELAY1600);
			OCR1B = PULSE_MAX;		// 180도
			_delay_ms(ROTATION_DELAY1600);
		}
		
		_delay_ms(100);
		
		LCD_clear();
	}
		
	


	return 0;
}
