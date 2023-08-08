// VGA pmod stuff
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VGA_ASYNC_FIFO_N_PIXELS 1
#include "vga/vga_pmod_async_pixels_fifo.c" // Version that expects only x,y pixels, N at a time

// Code for a shared AXI RAMs
// Tile down by 2,4,8 times etc to fit into on chip ram for now
#define TILE_FACTOR 4
#define TILE_FACTOR_LOG2 2
#define NUM_X_TILES (FRAME_WIDTH/TILE_FACTOR)
#define NUM_Y_TILES (FRAME_WIDTH/TILE_FACTOR)
#define BYTES_PER_PIXEL 4
#define BYTES_PER_PIXEL_LOG2 2
#define AXI_RAM_DEPTH (((NUM_X_TILES*NUM_Y_TILES)*BYTES_PER_PIXEL)/AXI_BUS_BYTE_WIDTH)
#include "shared_axi_brams.c"

// Pixel x,y pos to pixel index
uint32_t pos_to_pixel_index(uint16_t x, uint16_t y)
{
  uint16_t x_tile_index = x >> TILE_FACTOR_LOG2;
  uint16_t y_tile_index = y >> TILE_FACTOR_LOG2;
  return (y_tile_index*NUM_X_TILES) + x_tile_index;
}
// Pixel index to address in RAM
uint32_t pixel_index_to_addr(uint32_t index)
{
  // Each pixel is a 32b (4 byte) word
  uint32_t addr = index << BYTES_PER_PIXEL_LOG2;
  return addr;
}
// Pixel x,y to pixel ram address
uint32_t pos_to_addr(uint16_t x, uint16_t y)
{
  uint32_t pixel_index = pos_to_pixel_index(x, y);
  uint32_t addr = pixel_index_to_addr(pixel_index);
  return addr;
}

// Dual frame buffer is writing to one buffer while other is for reading
// wrappers around shared_resource_bus.h read() and write() dealing with request types etc
uint1_t frame_buffer_read_port_sel;
/* // Async pipelined start separate from finish
void frame_buf_read_start(uint16_t x, uint16_t y)
{
  uint32_t addr = pos_to_addr(x, y);
  dual_axi_ram_read_start(frame_buffer_read_port_sel, addr);
}
pixel_t frame_buf_read_finish()
{
  axi_ram_data_t read = dual_axi_ram_read_finish(frame_buffer_read_port_sel);
  pixel_t pixel;
  pixel.a = read.data[3];
  pixel.r = read.data[2];
  pixel.g = read.data[1];
  pixel.b = read.data[0];
  return pixel;
} */
/* // Read only versions for VGA port
void frame_buf_read_only_start(uint16_t x, uint16_t y)
{
  uint32_t addr = pos_to_addr(x, y);
  dual_axi_read_only_ram_read_start(frame_buffer_read_port_sel, addr);
}
pixel_t frame_buf_read_only_finish()
{
  axi_ram_data_t read = dual_axi_read_only_ram_read_finish(frame_buffer_read_port_sel);
  pixel_t pixel;
  pixel.a = read.data[3];
  pixel.r = read.data[2];
  pixel.g = read.data[1];
  pixel.b = read.data[0];
  return pixel;
}*/
// Blocking sync start a read and wait for it to finish
pixel_t frame_buf_read(uint16_t x, uint16_t y)
{
  uint32_t addr = pos_to_addr(x, y);
  axi_ram_data_t read = dual_axi_ram_read(frame_buffer_read_port_sel, addr);
  pixel_t pixel;
  pixel.a = read.data[3];
  pixel.r = read.data[2];
  pixel.g = read.data[1];
  pixel.b = read.data[0];
  return pixel;
}
/* Version using start then finish funcs - does work
pixel_t frame_buf_read(uint16_t x, uint16_t y)
{
  uint32_t addr = pos_to_addr(x, y);
  dual_axi_ram_read_start(frame_buffer_read_port_sel, addr);
  axi_ram_data_t read = dual_axi_ram_read_finish(frame_buffer_read_port_sel);
  pixel_t pixel;
  pixel.a = read.data[3];
  pixel.r = read.data[2];
  pixel.g = read.data[1];
  pixel.b = read.data[0];
  return pixel;
}*/
void frame_buf_write(uint16_t x, uint16_t y, pixel_t pixel)
{
  axi_ram_data_t write;
  write.data[3] = pixel.a;
  write.data[2] = pixel.r;
  write.data[1] = pixel.g;
  write.data[0] = pixel.b;
  uint32_t addr = pos_to_addr(x, y);
  dual_axi_ram_write(!frame_buffer_read_port_sel, addr, write);
}

// Always-reading logic to drive VGA signal into pmod_async_fifo_write

// HDL style version of VGA read start and finish (simple not derived FSM)
// TODO wrap into helper functions defined in shared_axi_rams.c
MAIN_MHZ(host_vga_read_starter, HOST_CLK_MHZ)
void host_vga_read_starter()
{
  // Increment VGA counters and do read for each position
  static vga_pos_t vga_pos;
  uint32_t addr = pos_to_addr(vga_pos.x, vga_pos.y);
  axi_ram0_read_req.data.user.araddr = addr;
  axi_ram1_read_req.data.user.araddr = addr;
  uint1_t do_increment = 0;
  // Read from the current read frame buffer
  axi_ram0_read_req.valid = 0;
  axi_ram1_read_req.valid = 0;
  if(frame_buffer_read_port_sel){
    axi_ram1_read_req.valid = 1;
    if(axi_ram1_read_req_ready){
      do_increment = 1;
    }
  }else{
    axi_ram0_read_req.valid = 1;
    if(axi_ram0_read_req_ready){
      do_increment = 1;
    }
  }
  vga_pos = vga_frame_pos_increment(vga_pos, do_increment);
}
MAIN_MHZ(host_vga_read_finisher, HOST_CLK_MHZ)
void host_vga_read_finisher()
{
  // Get read data from one of the AXI RAM busses
  static uint1_t read_port_sel;
  axi_read_data_t rd_data;
  uint1_t rd_data_valid = 0;
  if(read_port_sel){
    if(axi_ram1_read_data.valid){
      rd_data = axi_ram1_read_data.burst.data_resp.user;
      rd_data_valid = 1;
    }else{
      read_port_sel = !read_port_sel;
    }
  }else{
    if(axi_ram0_read_data.valid){
      rd_data = axi_ram0_read_data.burst.data_resp.user;
      rd_data_valid = 1;
    }else{
      read_port_sel = !read_port_sel;
    }
  }
  // Write pixel data into fifo
  pixel_t pixel;
  pixel.a = rd_data.rdata[3];
  pixel.r = rd_data.rdata[2];
  pixel.g = rd_data.rdata[1];
  pixel.b = rd_data.rdata[0];
  pixel_t pixels[1];
  pixels[0] = pixel;
  uint1_t fifo_ready = pmod_async_fifo_write_logic(pixels, rd_data_valid);
  axi_ram0_read_data_ready = 0;
  axi_ram1_read_data_ready = 0;
  if(read_port_sel){
    axi_ram1_read_data_ready = fifo_ready;
  }else{
    axi_ram0_read_data_ready = fifo_ready;
  }
}

/*
// FSM style version of VGA read start and finish
// max rate at output is 1 clock to finish read, 1 clock to write into VGA fifo
// so max rate is a read every other cycle
void host_vga_read_starter()
{
  vga_pos_t vga_pos;
  while(1)
  {
    // Start read of the 1 pixel at x,y pos
    frame_buf_read_only_start(vga_pos.x, vga_pos.y);
    // Increment vga pos for next time
    vga_pos = vga_frame_pos_increment(vga_pos, 1);
  }
}
void host_vga_read_finisher()
{
  while(1)
  {
    // Receive the output of the read started prior
    pixel_t pixels[1];
    pixels[0] = frame_buf_read_only_finish();
    
    // Write pixels into async fifo feeding vga pmod for display
    pmod_async_fifo_write(pixels);
  }
}
// Wrap up FSMs at top level
#include "host_vga_read_starter_FSM.h"
#include "host_vga_read_finisher_FSM.h"
MAIN_MHZ(host_vga_reader_wrapper, HOST_CLK_MHZ)
void host_vga_reader_wrapper()
{
  host_vga_read_starter_INPUT_t starter_i;
  starter_i.input_valid = 1;
  starter_i.output_ready = 1;
  host_vga_read_starter_OUTPUT_t starter_o = host_vga_read_starter_FSM(starter_i);
  host_vga_read_finisher_INPUT_t finisher_i;
  finisher_i.input_valid = 1;
  finisher_i.output_ready = 1;
  host_vga_read_finisher_OUTPUT_t finisher_o = host_vga_read_finisher_FSM(finisher_i);
}
*/
/* Single in flight always reading for VGA
void host_vga_reader()
{
  vga_pos_t vga_pos;
  while(1)
  {
    // Start read of the 1 pixel at x,y pos
    pixel_t pixels[1];
    pixels[0] = frame_buf_read(vga_pos.x, vga_pos.y);
    // Write pixels into async fifo feeding vga pmod for display
    pmod_async_fifo_write(pixels);
    // Increment vga pos for next time
    vga_pos = vga_frame_pos_increment(vga_pos, 1);
  }
}
#include "host_vga_reader_FSM.h"
MAIN_MHZ(host_vga_reader_wrapper, HOST_CLK_MHZ)
void host_vga_reader_wrapper()
{
  host_vga_reader_INPUT_t i;
  i.input_valid = 1;
  i.output_ready = 1;
  host_vga_reader_OUTPUT_t o = host_vga_reader_FSM(i);
}*/
