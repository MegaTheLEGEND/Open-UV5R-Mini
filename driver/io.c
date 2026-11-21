/* Open-UV5R-Mini: speaker amplifier, LEDs and lamp. See driver/io.h. */
#include "driver/io.h"
#include "board.h"
#include "bsp/ac695n.h"
#include "driver/gpio.h"

static uint8_t s_amp;
static bool s_lamp;

void IO_Init(void)
{
	GPIO_ConfigOutput(PIN_SPEAKER_AMP, false);
	GPIO_ConfigOutput(PIN_LED_RX, false);
	GPIO_ConfigOutput(PIN_LAMP, false);
	IO_LedTx(false);
	s_amp = 0;
	s_lamp = false;
}

void IO_Amp(uint8_t who, bool on)
{
	if (on)
		s_amp |= who;
	else
		s_amp &= (uint8_t)~who;
	GPIO_Write(PIN_SPEAKER_AMP, s_amp != 0);
}

void IO_LedRx(bool on) { GPIO_Write(PIN_LED_RX, on); }

/* PR0 through the power-domain port, as stock sub_01E5714E(0, on): output bit, then direction bit cleared. */
void IO_LedTx(bool on)
{
	uint8_t out = P33_Read(P33_PR_OUT);

	if (on)
		out |= 1u << LED_TX_PR_BIT;
	else
		out &= (uint8_t)~(1u << LED_TX_PR_BIT);
	P33_Write(P33_PR_OUT, out);
	P33_Write(P33_PR_DIR, P33_Read(P33_PR_DIR) & (uint8_t)~(1u << LED_TX_PR_BIT));
}

void IO_Lamp(bool on)
{
	s_lamp = on;
	GPIO_Write(PIN_LAMP, on);
}

bool IO_LampIsOn(void) { return s_lamp; }
