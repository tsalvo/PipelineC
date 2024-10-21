#include "fft.h"
#include "stream/stream.h"


// TODO I2S #include
DECL_STREAM_TYPE(i2s_samples_t)

// Types for the FIFO elements to be transfered
typedef struct fft_ram_2x_write_req_t{
  // t addr and data
  uint16_t t_index;
  fft_out_t t;
  uint1_t t_write_en;
  // u addr and data
  uint16_t u_index;
  fft_out_t u;
  uint1_t u_write_en;
}fft_ram_2x_write_req_t;
typedef struct fft_ram_2x_read_req_t{
  // t addr
  uint16_t t_index;
  // u addr
  uint16_t u_index;
}fft_ram_2x_read_req_t;
typedef struct fft_ram_2x_read_resp_t{
  // t data
  fft_out_t t;
  // u data
  fft_out_t u;
}fft_ram_2x_read_resp_t;

typedef enum fft_fsm_state_t{
  LOAD_INPUTS,
  BUTTERFLY_ITERS,
  UNLOAD_OUTPUTS
}fft_fsm_state_t;

DECL_STREAM_TYPE(fft_ram_2x_write_req_t)
DECL_STREAM_TYPE(fft_ram_2x_read_req_t)
DECL_STREAM_TYPE(fft_ram_2x_read_resp_t)
DECL_STREAM_TYPE(fft_2pt_w_omega_lut_in_t)
DECL_STREAM_TYPE(fft_out_t)

// Outputs
typedef struct fft_2pt_fsm_out_t
{
  // Stream of writes to RAM
  stream(fft_ram_2x_write_req_t) wr_reqs_to_ram;
  // Stream of read request addresses to RAM
  stream(fft_ram_2x_read_req_t) rd_addrs_to_ram;
  // Stream of data to butterfly pipeline
  stream(fft_2pt_w_omega_lut_in_t) data_to_pipeline;
  // Stream of output FFT result to CPU
  stream(fft_out_t) result_out;
  // Ready for input samples stream
  uint1_t ready_for_samples_in;
  // Ready for read resp from RAM
  uint1_t ready_for_rd_datas_from_ram;
  // Ready for data out from pipeline
  uint1_t ready_for_data_from_pipeline;
}fft_2pt_fsm_out_t;
fft_2pt_fsm_out_t fft_2pt_fsm(
  // Inputs
  // Stream of input samples from I2S
  i2s_samples_t_stream_t samples_in,
  // Stream of read response data from RAM
  stream(fft_ram_2x_read_resp_t) rd_datas_from_ram,
  // Stream of data from butterfly pipeline
  stream(fft_2pt_out_t) data_from_pipeline,
  // Ready for write req to RAM
  uint1_t ready_for_wr_reqs_to_ram,
  // Ready for read addr to RAM
  uint1_t ready_for_rd_addrs_to_ram,
  // Ready for data into pipeline
  uint1_t ready_for_data_to_pipeline,
  // Ready for result out to CPU
  uint1_t ready_for_result_out
){
  // State registers
  static fft_fsm_state_t state;
  //  FFT s,j,k iterators for various streams
  static fft_iters_t rd_req_iters;
  static fft_iters_t pipeline_req_iters;
  static fft_iters_t wr_req_iters; 
  //  Some helper flags to do 's' loops sequentially
  static uint1_t waiting_on_s_iter_to_finish;
  static uint1_t rd_reqs_done;
  // Outputs (default all zeros)
  fft_2pt_fsm_out_t o; 
  // FSM logic
  if(state==LOAD_INPUTS)
  {
    // Load input samples from I2S
    // data needs fixed point massaging
    // and only using the left channel as real inputs to FFT
    fft_data_t data_in = samples_in.data.l >> (24-16); // TODO ifdef float then error
    // Array index is bit reversed, using 'j' as sample counter here
    uint32_t array_index = wr_req_iters.j(0, 31); // reverse of [31:0]
    // Connect input samples stream
    // to the stream of writes going to RAM
    // only using one 't' part of two possible write addrs+datas
    // Data (connect input to output)
    o.wr_reqs_to_ram.data.t.real = data_in;
    o.wr_reqs_to_ram.data.t_index = array_index;
    o.wr_reqs_to_ram.data.t_write_en = 1;
    // Valid (connect input to output)
    o.wr_reqs_to_ram.valid = samples_in.valid;
    // Ready (connect input to output)
    o.ready_for_samples_in = ready_for_wr_reqs_to_ram;
    // Transfering data this cycle (valid&ready)?
    if(samples_in.valid & o.ready_for_samples_in){
      // Last sample into ram?
      if(wr_req_iters.j==(NFFT-1)){
        // Done loading samples, next state
        state = BUTTERFLY_ITERS;
        wr_req_iters = FFT_ITERS_INIT;
      }else{
        // More input samples coming
        wr_req_iters.j += 1;
      }
    }
  }
  else if(state==BUTTERFLY_ITERS)
  {
    // FSM version of s,k,j loops from fft.c
    // that stream data through fft_2pt_w_omega_lut pipeline
    // Note: 's' loops must be done sequentially:
    // meaning need to confirm all iters from one 's' iter
    // are done before starting another
    /*
    fft_2pt_w_omega_lut_in_t fft_in;
    fft_in.t = output[t_index];
    fft_in.u = output[u_index];
    fft_in.s = s;
    fft_in.j = j;
    fft_2pt_out_t fft_out = fft_2pt_w_omega_lut(fft_in);
    */
    // 1)
    // Begins by making valid requests to read data from RAM
    // i.e. lookup output[t_index], output[u_index]
    uint32_t m = 1 << rd_req_iters.s;
    uint32_t m_1_2 = m >> 1;
    uint32_t t_index = rd_req_iters.k + rd_req_iters.j + m_1_2;
    uint32_t u_index = rd_req_iters.k + rd_req_iters.j;
    // until done making requests 
    // or unless waiting for s iter to finish 
    if(~rd_reqs_done & ~waiting_on_s_iter_to_finish){
      // Data
      o.rd_addrs_to_ram.data.t_index = t_index;
      o.rd_addrs_to_ram.data.u_index = u_index;
      // Valid
      o.rd_addrs_to_ram.valid = 1;
      // Ready, transfering data this cycle (valid&ready)?
      if(ready_for_rd_addrs_to_ram){
        // Transfer going through, next
        // Ending an s iteraiton?
        uint1_t s_incrementing = k_last(rd_req_iters) & j_last(rd_req_iters);
        if(s_incrementing){
          // Pause, wait for current s iter to finish
          waiting_on_s_iter_to_finish = 1;
        }
        // Last req to ram?
        if(last_iter(rd_req_iters)){
          // Done requests
          rd_reqs_done = 1;
          rd_req_iters = FFT_ITERS_INIT;
        }else{
          // More reqs to make
          rd_req_iters = next_iters(rd_req_iters);
        }
      }
    }
    // 2)
    // Read response data from RAM connected to butterfly pipeline
    // Data (connect input to output)
    o.data_to_pipeline.data.t = rd_datas_from_ram.data.t;
    o.data_to_pipeline.data.u = rd_datas_from_ram.data.u;
    //  along with some iterators counting along
    o.data_to_pipeline.data.j = pipeline_req_iters.j;
    o.data_to_pipeline.data.s = pipeline_req_iters.s;
    // Valid (connect input to output)
    o.data_to_pipeline.valid = rd_datas_from_ram.valid;
    // Ready (connect input to output)
    o.ready_for_rd_datas_from_ram = ready_for_data_to_pipeline;
    // Transfering data this cycle (valid&ready)?
    if(o.data_to_pipeline.valid & ready_for_data_to_pipeline){
      // Transfer going through, count next
      pipeline_req_iters = next_iters(pipeline_req_iters);
      // Last req to pipeline?
      if(last_iter(pipeline_req_iters)){
        // resets counters
        pipeline_req_iters = FFT_ITERS_INIT;
      }
    }
    // 3) 
    // Pipeline outputs are connected to RAM writes
    // i.e. write back output[t_index], output[u_index] to RAM
    uint32_t m = 1 << wr_req_iters.s;
    uint32_t m_1_2 = m >> 1;
    uint32_t t_index = wr_req_iters.k + wr_req_iters.j + m_1_2;
    uint32_t u_index = wr_req_iters.k + wr_req_iters.j;
    // Data (connect input to output)
    o.wr_reqs_to_ram.data.t = data_from_pipeline.data.t;
    o.wr_reqs_to_ram.data.t_write_en = 1;
    o.wr_reqs_to_ram.data.u = data_from_pipeline.data.u;
    o.wr_reqs_to_ram.data.u_write_en = 1;
    //  along with some iterators counting along
    o.wr_reqs_to_ram.data.t_index = t_index;
    o.wr_reqs_to_ram.data.u_index = u_index;
    // Valid (connect input to output)
    o.wr_reqs_to_ram.valid = data_from_pipeline.valid;
    // Ready (connect input to output)
    o.ready_for_data_from_pipeline = ready_for_wr_reqs_to_ram;
    // Transfering data this cycle (valid&ready)?
    if(o.wr_reqs_to_ram.valid & ready_for_wr_reqs_to_ram){
      // Transfer going through, next
      // Ending an s iteraiton?
      uint1_t s_incrementing = k_last(wr_req_iters) & j_last(wr_req_iters);
      if(s_incrementing){
        // Finished s iteration write back now
        waiting_on_s_iter_to_finish = 0;
      }
      // Last req to ram?
      if(last_iter(wr_req_iters)){
        // Done write back, finishes FFT, unload result
        wr_req_iters = FFT_ITERS_INIT;
        state = UNLOAD_OUTPUTS;
      }else{
        // More write reqs to make
        wr_req_iters = next_iters(wr_req_iters);
      }
    }
  }
  else // UNLOAD_OUTPUTS
  {
    // Start/request reads of data from RAM to output to CPU
    // (using 'j' counter for this)
    if(rd_req_iters.j < NFFT){
      // Try to start next read by making valid request
      // Data (only using one 't' part of two possible read addrs)
      o.rd_addrs_to_ram.data.t_index = rd_req_iters.j;
      // Valid
      o.rd_addrs_to_ram.valid = 1;
      // Ready
      if(ready_for_rd_addrs_to_ram){
        // Transfer going through, next
        rd_req_iters.j += 1;
        // move to next state handled below with read responses        
      }
    }
    // Responses from RAM connected to CPU
    // (using 'k' for this)
    if(rd_req_iters.k < NFFT){
      // Connect responses from RAM to CPU output
      // Data (only using one 't' part of two possible read datas)
      o.result_out.data = rd_datas_from_ram.data.t;
      // Valid (connect input to output) 
      o.result_out.valid = rd_datas_from_ram.valid;
      // Ready (connect input to output) 
      o.ready_for_rd_datas_from_ram = ready_for_result_out;
      // Transfering data this cycle (valid&ready)?
      if(o.result_out.valid & ready_for_result_out){
        // Last data to CPU?
        if(rd_req_iters.k==(NFFT-1)){
          // Done offloading output, restart FFT
          state = LOAD_INPUTS;
          rd_req_iters = FFT_ITERS_INIT;
          rd_reqs_done = 0;
        }else{
          // Still waiting for more outputs
          rd_req_iters.k += 1;
        }
      }
    }
  }
  return o;
}

