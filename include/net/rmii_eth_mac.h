#include "uintN_t.h"  // uintN_t types for any N
#include "compiler.h"
#include "axi/axis.h"

typedef enum rmii_rx_mac_state_t{
  IDLE,
  PREAMBLE,
  //SFD,
  DATA,
  FCS
}rmii_rx_mac_state_t;

// RX-MAC Outputs
typedef struct rmii_rx_mac_t{
  stream(axis8_t) rx_mac_axis_out;
  uint1_t rx_mac_error;
}rmii_rx_mac_t;
#pragma FUNC_MARK_DEBUG rmii_rx_mac
rmii_rx_mac_t rmii_rx_mac(
  // RX-MAC Inputs
  uint2_t data_in,
  uint1_t data_in_valid
){
  uint1_t last = 0;
  uint1_t valid = 0;
  uint1_t err = 0;
  // Per the LAN8720 datasheet figure 3-3 (is this RMII standard :-/?)
  // the data valid signal is deasserted two 4b nibbles (a byte)
  // after the data of the frame (which includes 4 bytes of CRC)
  // Need to have delay regs long enough for CRC 
  // and the last byte before valid is deasserted (5 bytes total)
  // to detect the tlast end of payload data appropriately
  #define RMII_ETH_MAC_N_INPUT_REGS ((5*8)/2) // 5 bytes, 2b words
  static uint2_t data_in_regs[RMII_ETH_MAC_N_INPUT_REGS];
  static uint1_t data_in_valid_regs[RMII_ETH_MAC_N_INPUT_REGS];
  uint2_t data_in_delayed = data_in_regs[RMII_ETH_MAC_N_INPUT_REGS-1];
  uint1_t data_in_valid_delayed = data_in_valid_regs[RMII_ETH_MAC_N_INPUT_REGS-1];
  static uint8_t data_out_reg;
  static rmii_rx_mac_state_t state;
  static uint3_t bit_counter;
  static uint32_t byte_counter;
  static uint32_t fcs_reg;
  rmii_rx_mac_t o;

  uint1_t bit_end = (bit_counter == 6);
  uint1_t preamble_bits = (data_in_valid_delayed && data_in_delayed == 0b01);
  uint1_t sfd_bits = (data_in_valid_delayed && data_in_delayed == 0b11);

  if(state == IDLE){ // IDLE (Find Preamble)
    last = 0;
    valid = 0;
    data_out_reg = 0;
    bit_counter = 0;
    byte_counter = 0;
    fcs_reg = 0;
    if(preamble_bits){ // preamble start
      err = 0; // reset error ?
      state = PREAMBLE; // Goto Preamble
    }
  }
  else if(state == PREAMBLE){ // PREAMBLE (Find SFD)
    if(preamble_bits){
      state = PREAMBLE; // Repeat Preamble
    }
    else if(sfd_bits){
      state = DATA; // Found SFD goto DATA
    }
    else{
      state = IDLE; // ERROR
      err = 1;
    }
  }
  else if(state == DATA){ // DATA
    if(data_in_valid_delayed){
      data_out_reg = uint2_uint6(data_in_delayed,data_out_reg(7,2));
      valid = bit_end;
      if(bit_end){
        byte_counter += 1;
        bit_counter = 0;
        // Frame end
        // If no more bits next cycle 
        // (checking not register delayed version of valid)
        // then this was last byte this cycle
        // TODO not checking length or FCS
        if(~data_in_valid){
          last = 1;
          bit_counter = 0; // reset for crc
          byte_counter = 0; // reset for crc
          state = FCS; // Goto FCS
          if(byte_counter < 64){ // Frame too short
            err = 1; // error
          } else if(byte_counter >= 1517){ // Frame too long
            err = 1; // error
          }
        }
      }
      else{
        bit_counter += 2;
      }  
    }
  }
  else if(state == FCS){
    // Just wait through CRC bytes
    // save data for debug only
    fcs_reg = uint2_uint30(data_in_delayed,fcs_reg>>2);
    data_out_reg = 0;
    if(bit_end){
      if(byte_counter == 3){
        state = IDLE; // Goto IDLE
      }else{
        byte_counter += 1;
      }
      bit_counter = 0;
    }
    else{
      bit_counter += 2;
    } 
  }

  // Input delay registers
  ARRAY_1SHIFT_INTO_BOTTOM(data_in_regs, RMII_ETH_MAC_N_INPUT_REGS, data_in)
  ARRAY_1SHIFT_INTO_BOTTOM(data_in_valid_regs, RMII_ETH_MAC_N_INPUT_REGS, data_in_valid)

  // AXIS Output
  o.rx_mac_axis_out.data.tdata[0] = data_out_reg;
  o.rx_mac_axis_out.data.tlast = last;
  o.rx_mac_axis_out.data.tkeep[0] = valid;
  o.rx_mac_axis_out.valid = valid;
  
  o.rx_mac_error = err;
  return o;
}

typedef enum rmii_tx_mac_state_t{
  IDLE,
  PREAMBLE,
  SFD,
  DATA,
  FCS
  // TODO IPG
}rmii_tx_mac_state_t;

// AXI-S 8bit TX-MAC Outputs
typedef struct rmii_tx_mac_t{
  uint2_t tx_mac_output_data;
  uint1_t tx_mac_output_valid;
  uint1_t tx_mac_input_ready;
}rmii_tx_mac_t;
#pragma FUNC_MARK_DEBUG rmii_tx_mac
rmii_tx_mac_t rmii_tx_mac(
  // AXI-S 8bit TX-MAC Inputs
  stream(axis8_t) axis_in
){
  // AXI-S TX-MAC FSM
  // Gen. full Eth Frame
  // Appends PREAMBLE + SFD
  // Appends Data
  // Appends FCS
  static rmii_tx_mac_state_t state;
  static uint8_t counter;
  static uint32_t crc32;
  static uint32_t crc32_debug;
  static uint8_t data_reg;
  static uint1_t last_byte_reg;
  rmii_tx_mac_t o;
  uint32_t POLY = 0x04C11DB7;
  //uint8_t crc32_4[4] = {crc32(0,7), crc32(8,15), crc32(16,23), crc32(24,31)};
  uint1_t preamble_ctr_end = (counter == ((((7*8)/2)+3)-1)); // 7 bytes, 2b words PLUS extra 3 2b words of SFD
  uint1_t fcs_ctr_end = (counter == (((4*8)/2)-1)); // 4 bytes, 2b words

  o.tx_mac_output_data = 0;
  o.tx_mac_output_valid = 0;
  o.tx_mac_input_ready = 0;

  if(state == IDLE){ // IDLE
    crc32 = 0xFFFFFFFF;
    if(axis_in.valid){
      state = PREAMBLE; // Send preamble if ready
    }
  }
  else if(state == PREAMBLE){ // PREAMBLE
    o.tx_mac_output_data = 0b01;
    o.tx_mac_output_valid = 1;
    if(preamble_ctr_end){
      state = SFD; // Goto SFD
      counter = 0;
    }
    else{
      counter += 1;
    }
  }
  else if(state == SFD){ // SFD
    o.tx_mac_output_valid = 1;
    o.tx_mac_output_data = 0b11;
    state = DATA; // Goto DATA
    // Take input data this cycle to serialize next cycle
    uint8_t b = axis_in.data.tdata[0];
    b = (b << 4) | (b >> 4);
    data_reg = b;
    last_byte_reg = axis_in.data.tlast;
    o.tx_mac_input_ready = 1;
    counter = 0;
  }
  else if(state == DATA){ // DATA
    // Output bottom two bits of data reg
    o.tx_mac_output_data = data_reg(1,0);
    o.tx_mac_output_valid = 1;
    // Last two bits of data byte?
    uint1_t last_bits_of_byte = (counter == 6);
    uint1_t last_bits_of_last_byte = last_bits_of_byte & last_byte_reg;
    if(last_bits_of_byte){
      if(last_bits_of_last_byte){
        state = FCS; // Goto FCS
        counter = 0;
      }else{
        // Next byte coming
        // Take input data this cycle to serialize next
        uint8_t b = axis_in.data.tdata[0];
        b = (b << 4) | (b >> 4);
        data_reg = b;
        last_byte_reg = axis_in.data.tlast;
        o.tx_mac_input_ready = 1;
        counter = 0;
      }
    }
    else{
      // Next two bits of byte
      counter += 2;
      data_reg = data_reg >> 2;
    }
    // Compute CRC as axis in data is registered
    if(axis_in.valid & o.tx_mac_input_ready){
      // TODO is this correct CRC math?
      uint32_t crc_next = crc32 ^ (uint32_t)(axis_in.data.tdata[0]); // XOR input data
      uint4_t i = 0;
      for (i = 0; i < 8; i = i + 1){
          if (crc_next(31)){
            crc_next = (crc_next << 1) ^ POLY;
          }
          else{
            crc_next = crc_next << 1;
          }
      }
      crc32 = crc_next;
    }
    crc32_debug = crc32;
  }
  else if(state == FCS){ // FCS
    // Output bottom two bits of 32b CRC data
    o.tx_mac_output_data = crc32(1,0);
    o.tx_mac_output_valid = 1;
    // Last two bits of CRC bytes?
    if(fcs_ctr_end){
      state = IDLE; // Goto IDLE
      counter = 0;
    }
    else{
      // Next two bits of byte
      counter += 2;
      crc32 = crc32 >> 2;
    }
  }
  return o;
}
