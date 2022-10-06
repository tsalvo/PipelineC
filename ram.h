#pragma once
#include "xstr.h" // xstr func for putting quotes around macro things

// This file includes preprocessor helper hacks to help with/work around
// simple single and dual port generated RAM template functions that exist: 
// https://github.com/JulianKemmerer/PipelineC/wiki/Automatically-Generated-Functionality#rams

// Ideally this could all be autogenerated: https://github.com/JulianKemmerer/PipelineC/issues/121

#define RAM_INIT_INT_ZEROS "(others => (others => '0'))"

// Dual port, one read+write, one read only, 0 latency
#define DECL_RAM_DP_RW_R_0( \
  elem_t, \
  ram_name, \
  SIZE, \
  VHDL_INIT \
) \
typedef struct ram_name##_outputs_t \
{ \
  elem_t rd_data0; \
  elem_t rd_data1; \
}ram_name##_outputs_t; \
ram_name##_outputs_t ram_name( \
  uint32_t addr0, \
  elem_t wr_data0, uint1_t wr_en0, \
  uint32_t addr1 \
){ \
  __vhdl__("\n\
  constant SIZE : integer := " xstr(SIZE) "; \n\
  type ram_t is array(0 to SIZE-1) of " xstr(elem_t) "; \n\
  signal the_ram : ram_t := " VHDL_INIT "; \n\
  -- Limit zero latency comb. read addr range to SIZE \n\
  -- since invalid addresses can occur as logic propogates \n\
  -- (this includes out of int32 range u32 values) \n\
  signal addr0_s : integer range 0 to SIZE-1 := 0; \n\
  signal addr1_s : integer range 0 to SIZE-1 := 0; \n\
begin \n\
  process(all) begin \n\
    addr0_s <= to_integer(addr0(30 downto 0)) \n\
    -- synthesis translate_off \n\
    mod SIZE \n\
    -- synthesis translate_on \n\
    ; \n\
    addr1_s <= to_integer(addr1(30 downto 0)) \n\
    -- synthesis translate_off \n\
    mod SIZE \n\
    -- synthesis translate_on \n\
    ; \n\
  end process; \n\
  process(clk) is \n\
  begin \n\
    if rising_edge(clk) then \n\
      if CLOCK_ENABLE(0)='1' then \n\
        if wr_en0(0) = '1' then \n\
          the_ram(addr0_s) <= wr_data0; \n\
        end if; \n\
      end if; \n\
    end if; \n\
  end process; \n\
  return_output.rd_data0 <= the_ram(addr0_s); \n\
  return_output.rd_data1 <= the_ram(addr1_s); \n\
"); \
}

// Triple port, two read only, one write only, 0 latency
#define DECL_RAM_TP_R_R_W_0( \
  elem_t, \
  ram_name, \
  SIZE, \
  VHDL_INIT \
) \
typedef struct ram_name##_out_t \
{ \
  elem_t rd_data0; \
  elem_t rd_data1; \
}ram_name##_out_t; \
ram_name##_out_t ram_name( \
  uint32_t rd_addr0, \
  uint32_t rd_addr1, \
  uint32_t wr_addr, elem_t wr_data, uint1_t wr_en \
){ \
  __vhdl__("\n\
  constant SIZE : integer := " xstr(SIZE) "; \n\
  type ram_t is array(0 to SIZE-1) of " xstr(elem_t) "; \n\
  signal the_ram : ram_t := " VHDL_INIT "; \n\
  -- Limit zero latency comb. read addr range to SIZE \n\
  -- since invalid addresses can occur as logic propogates \n\
  -- (this includes out of int32 range u32 values) \n\
  signal rd_addr0_s : integer range 0 to SIZE-1 := 0; \n\
  signal rd_addr1_s : integer range 0 to SIZE-1 := 0; \n\
begin \n\
  process(all) begin \n\
    rd_addr0_s <= to_integer(rd_addr0(30 downto 0)) \n\
    -- synthesis translate_off \n\
    mod SIZE \n\
    -- synthesis translate_on \n\
    ; \n\
    rd_addr1_s <= to_integer(rd_addr1(30 downto 0)) \n\
    -- synthesis translate_off \n\
    mod SIZE \n\
    -- synthesis translate_on \n\
    ; \n\
  end process; \n\
  process(clk) \n\
  begin \n\
    if rising_edge(clk) then \n\
      if CLOCK_ENABLE(0)='1' then \n\
        if wr_en(0) = '1' then \n\
          the_ram(to_integer(wr_addr)) <= wr_data; \n\
        end if; \n\
      end if; \n\
    end if; \n\
  end process; \n\
  return_output.rd_data0 <= the_ram(rd_addr0_s); \n\
  return_output.rd_data1 <= the_ram(rd_addr1_s); \n\
"); \
}
