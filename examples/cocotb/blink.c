#include "uintN_t.h"  // uintN_t types for any N

// Install+configure synthesis tool then specify part here
// #pragma PART "xc7a35ticsg324-1l"

// Debug ports for helper generated cocotb python
// ex. return counter_debug(dut) # cocotb code to read debug wire
#include "debug_port.h"
DEBUG_OUTPUT_DECL(uint25_t, counter_debug)
DEBUG_OUTPUT_DECL(uint1_t, led_debug)

#pragma MAIN_MHZ blink 100.0
uint1_t blink()
{
  // Time counter registers
  static uint25_t counter;
  // LED on off state registers
  static uint1_t led;
  //printf("Counter: %d, LED: %d\n", counter, led);
  // Connect to debug ports
  counter_debug = counter;
  led_debug = led;
  // If reached timeout (short for demo)
  if(counter==(3-1))
  {
    // Toggle led
    led = !led;
    // Reset counter
    counter = 0;
  }
  else
  {
    counter += 1;
  }
  return led;
}
