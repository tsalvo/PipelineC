// See README.md for info on this design
#include "intN_t.h"
#include "uintN_t.h"

// int16_t w/ valid flag
typedef struct i16_stream_t{
  int16_t data;
  uint1_t valid;
} i16_stream_t;

// complex int16_t struct
typedef struct ci16_t{
  int16_t real;
  int16_t imag;
} ci16_t;

// complex int16_t struct w/ valid flag
typedef struct ci16_stream_t{
  ci16_t data;
  uint1_t valid;
} ci16_stream_t;

// Declare decimation FIR modules to use
// radio front end to single FM radio channel ~300KSPS
// 5x Decim
#define fir_decim_name decim_5x
#define FIR_DECIM_N_TAPS 49
#define FIR_DECIM_LOG2_N_TAPS 6
#define FIR_DECIM_FACTOR 5
#define fir_decim_data_t int16_t
#define fir_decim_coeff_t int16_t
#define fir_decim_accum_t int38_t // data_width + coeff_width + log2(taps#)
#define fir_decim_out_t int16_t
#define FIR_DECIM_POW2_DN_SCALE 15 // data_width + coeff_width - out_width - 1
#define FIR_DECIM_COEFFS { \
  -165, \
  -284, \
  -219, \
  -303, \
  -190, \
  -102, \
  98,   \
  268,  \
  430,  \
  482,  \
  420,  \
  207,  \
  -117, \
  -498, \
  -835, \
  -1022,\
  -957, \
  -576, \
  135,  \
  1122, \
  2272, \
  3429, \
  4419, \
  5086, \
  5321, \
  5086, \
  4419, \
  3429, \
  2272, \
  1122, \
  135,  \
  -576, \
  -957, \
  -1022,\
  -835, \
  -498, \
  -117, \
  207,  \
  420,  \
  482,  \
  430,  \
  268,  \
  98,   \
  -102, \
  -190, \
  -303, \
  -219, \
  -284, \
  -165  \
}            
#include "dsp/fir_decim.h"
#define decim_5x_out_t fir_decim_out_data_stream_type(decim_5x)
#define decim_5x_in_t fir_decim_in_data_stream_type(decim_5x)
// 10x decim
#define fir_decim_name decim_10x
#define FIR_DECIM_N_TAPS 95
#define FIR_DECIM_LOG2_N_TAPS 7
#define FIR_DECIM_FACTOR 10
#define fir_decim_data_t int16_t
#define fir_decim_coeff_t int16_t
#define fir_decim_accum_t int39_t // data_width + coeff_width + log2(taps#)
#define fir_decim_out_t int16_t
#define FIR_DECIM_POW2_DN_SCALE 15 // data_width + coeff_width - out_width - 1
#define FIR_DECIM_COEFFS { \
  -199,\
  -90, \
  -103,\
  -113,\
  -116,\
  -113,\
  -101,\
  -82, \
  -54, \
  -19, \
  22,  \
  68,  \
  115, \
  161, \
  202, \
  235, \
  257, \
  265, \
  257, \
  231, \
  187, \
  126, \
  50,  \
  -37, \
  -132,\
  -227,\
  -318,\
  -396,\
  -456,\
  -490,\
  -493,\
  -459,\
  -385,\
  -269,\
  -111,\
  86,  \
  318, \
  579, \
  860, \
  1154,\
  1448,\
  1732,\
  1995,\
  2227,\
  2418,\
  2560,\
  2648,\
  2678,\
  2648,\
  2560,\
  2418,\
  2227,\
  1995,\
  1732,\
  1448,\
  1154,\
  860, \
  579, \
  318, \
  86,  \
  -111,\
  -269,\
  -385,\
  -459,\
  -493,\
  -490,\
  -456,\
  -396,\
  -318,\
  -227,\
  -132,\
  -37, \
  50,  \
  126, \
  187, \
  231, \
  257, \
  265, \
  257, \
  235, \
  202, \
  161, \
  115, \
  68,  \
  22,  \
  -19, \
  -54, \
  -82, \
  -101,\
  -113,\
  -116,\
  -113,\
  -103,\
  -90, \
  -199 \
}
#include "dsp/fir_decim.h"
#define decim_10x_out_t fir_decim_out_data_stream_type(decim_10x)
#define decim_10x_in_t fir_decim_in_data_stream_type(decim_10x)


typedef struct window_t{
    ci16_t data[3];
} window_t;

window_t samples_window(ci16_stream_t iq){
  static window_t state;
  if(iq.valid){
    // shift data (reverse order with save)
    state.data[2] = state.data[1];
    state.data[1] = state.data[0];
    // save input sample
    state.data[0] = iq.data;
  }
  return state;
}

// FM demodulation using differentiator
// TODO fix FM dev and sample rate for real radio FM data?
//#define FM_DEV_HZ 25.0 
//#define SAMPLE_RATE_HZ 1000.0
i16_stream_t fm_demodulate(ci16_stream_t iq_sample){
  window_t multi_samples = samples_window(iq_sample);
  int16_t i_dot = multi_samples.data[0].real - multi_samples.data[2].real;
  int16_t q_dot = multi_samples.data[0].imag - multi_samples.data[2].imag;
  int16_t output_a = (multi_samples.data[1].real * q_dot) >> 15;
  int16_t output_b = (multi_samples.data[1].imag * i_dot) >> 15;
  int16_t output = output_a - output_b;
  /*// Output scaling factor
  float df = FM_DEV_HZ/SAMPLE_RATE_HZ;
  float scale_factor_f = 1.0 / (2.0 * 3.14 * df); // 1/(2 pi df)
  float f_i16_max = (float)(((int16_t)1<<15)-1);
  int32_t scale_factor_qN_15 = (int32_t)(scale_factor_f * f_i16_max);
  int16_t scaled_output_q1_15 = (output * scale_factor_qN_15) >> 15;*/
  i16_stream_t output_stream = {
    .data = output,
    .valid = iq_sample.valid
  };
  return output_stream;
}

// Interpolation Part of (24/125) sample rate change
#define fir_interp_name interp_24x
#define FIR_INTERP_N_TAPS 227
#define FIR_INTERP_LOG2_N_TAPS 8
#define FIR_INTERP_FACTOR 24 
#define fir_interp_data_t int16_t
#define fir_interp_coeff_t int16_t
#define fir_interp_accum_t int40_t // data_width + coeff_width + log2(taps#)
#define fir_interp_out_t int16_t
#define FIR_INTERP_OUT_SCALE 3 // normalize, // 3x then 8x(<<3) w pow2 scale = 24
#define FIR_INTERP_POW2_DN_SCALE (15-3) // data_width + coeff_width - out_width - 1 // fixed point adjust
#define FIR_INTERP_COEFFS { \
  -173, \
  -34,  \
  -36,  \
  -39,  \
  -41,  \
  -44,  \
  -45,  \
  -47,  \
  -48,  \
  -49,  \
  -49,  \
  -49,  \
  -48,  \
  -46,  \
  -44,  \
  -42,  \
  -38,  \
  -35,  \
  -30,  \
  -25,  \
  -20,  \
  -13,  \
  -7,   \
  0,    \
  7,    \
  15,   \
  23,   \
  31,   \
  39,   \
  48,   \
  56,   \
  64,   \
  71,   \
  78,   \
  85,   \
  91,   \
  97,   \
  101,  \
  105,  \
  108,  \
  110,  \
  110,  \
  110,  \
  108,  \
  105,  \
  100,  \
  95,   \
  88,   \
  79,   \
  70,   \
  59,   \
  47,   \
  34,   \
  20,   \
  6,    \
  -10,  \
  -26,  \
  -42,  \
  -59,  \
  -75,  \
  -92,  \
  -108, \
  -124, \
  -139, \
  -153, \
  -166, \
  -177, \
  -187, \
  -195, \
  -201, \
  -205, \
  -207, \
  -206, \
  -203, \
  -197, \
  -188, \
  -176, \
  -161, \
  -143, \
  -122, \
  -97,  \
  -70,  \
  -40,  \
  -8,   \
  28,   \
  66,   \
  107,  \
  150,  \
  194,  \
  241,  \
  289,  \
  338,  \
  389,  \
  440,  \
  491,  \
  542,  \
  593,  \
  643,  \
  693,  \
  741,  \
  787,  \
  831,  \
  873,  \
  913,  \
  949,  \
  983,  \
  1013, \
  1040, \
  1063, \
  1082, \
  1096, \
  1107, \
  1114, \
  1116, \
  1114, \
  1107, \
  1096, \
  1082, \
  1063, \
  1040, \
  1013, \
  983,  \
  949,  \
  913,  \
  873,  \
  831,  \
  787,  \
  741,  \
  693,  \
  643,  \
  593,  \
  542,  \
  491,  \
  440,  \
  389,  \
  338,  \
  289,  \
  241,  \
  194,  \
  150,  \
  107,  \
  66,   \
  28,   \
  -8,   \
  -40,  \
  -70,  \
  -97,  \
  -122, \
  -143, \
  -161, \
  -176, \
  -188, \
  -197, \
  -203, \
  -206, \
  -207, \
  -205, \
  -201, \
  -195, \
  -187, \
  -177, \
  -166, \
  -153, \
  -139, \
  -124, \
  -108, \
  -92,  \
  -75,  \
  -59,  \
  -42,  \
  -26,  \
  -10,  \
  6,    \
  20,   \
  34,   \
  47,   \
  59,   \
  70,   \
  79,   \
  88,   \
  95,   \
  100,  \
  105,  \
  108,  \
  110,  \
  110,  \
  110,  \
  108,  \
  105,  \
  101,  \
  97,   \
  91,   \
  85,   \
  78,   \
  71,   \
  64,   \
  56,   \
  48,   \
  39,   \
  31,   \
  23,   \
  15,   \
  7,    \
  0,    \
  -7,   \
  -13,  \
  -20,  \
  -25,  \
  -30,  \
  -35,  \
  -38,  \
  -42,  \
  -44,  \
  -46,  \
  -48,  \
  -49,  \
  -49,  \
  -49,  \
  -48,  \
  -47,  \
  -45,  \
  -44,  \
  -41,  \
  -39,  \
  -36,  \
  -34,  \
  -173  \
}
#include "dsp/fir_interp.h"
#define interp_24x_out_t fir_interp_out_data_stream_type(interp_24x)
#define interp_24x_in_t fir_interp_in_data_stream_type(interp_24x)


// Declare 5x decim FIR module to use in final decim to audio rate
// Includes low pass of just up to 15Khz. TW@11KHz,STOP@15KHz 
#define fir_decim_name decim_5x_audio
#define FIR_DECIM_N_TAPS 93
#define FIR_DECIM_LOG2_N_TAPS 7
#define FIR_DECIM_FACTOR 5
#define fir_decim_data_t int16_t
#define fir_decim_coeff_t int16_t
#define fir_decim_accum_t int39_t // data_width + coeff_width + log2(taps#)
#define fir_decim_out_t int16_t
#define FIR_DECIM_POW2_DN_SCALE 15 // data_width + coeff_width - out_width - 1
#define FIR_DECIM_COEFFS { \
  215, \
  119, \
  138, \
  149, \
  149, \
  135, \
  107, \
  65,  \
  14,  \
  -44, \
  -102,\
  -154,\
  -195,\
  -217,\
  -217,\
  -192,\
  -142,\
  -69, \
  19,  \
  114, \
  207, \
  285, \
  339, \
  357, \
  335, \
  268, \
  160, \
  16,  \
  -152,\
  -327,\
  -490,\
  -621,\
  -700,\
  -708,\
  -632,\
  -463,\
  -200,\
  150, \
  574, \
  1050,\
  1552,\
  2050,\
  2512,\
  2909,\
  3214,\
  3406,\
  3471,\
  3406,\
  3214,\
  2909,\
  2512,\
  2050,\
  1552,\
  1050,\
  574, \
  150, \
  -200,\
  -463,\
  -632,\
  -708,\
  -700,\
  -621,\
  -490,\
  -327,\
  -152,\
  16,  \
  160, \
  268, \
  335, \
  357, \
  339, \
  285, \
  207, \
  114, \
  19,  \
  -69, \
  -142,\
  -192,\
  -217,\
  -217,\
  -195,\
  -154,\
  -102,\
  -44, \
  14,  \
  65,  \
  107, \
  135, \
  149, \
  149, \
  138, \
  119, \
  215  \
}            
#include "dsp/fir_decim.h"
#define decim_5x_audio_out_t fir_decim_out_data_stream_type(decim_5x_audio)
#define decim_5x_audio_in_t fir_decim_in_data_stream_type(decim_5x_audio)


// Deemphasis
// 75e-6 Tau (USA)
// 50e-6 Tau (EU)
#define USA
#if defined(USA)
#define fm_alpha (int16_t)(7123)
#else
#define fm_alpha (int16_t)(9637)
#endif // TAU
i16_stream_t deemphasis_wfm(i16_stream_t input, int16_t input_mult_alpha){
  // IO regs to meet timing instead of re-writing for autopipelining somehow
  static i16_stream_t input_r;
  static i16_stream_t output_r;
  static int16_t input_mult_alpha_r;

  // Output reg
  i16_stream_t output_stream = output_r;

  /*
      Sample rate: 48000Hz
      typical time constant (tau) values:
      WFM transmission in USA: 75 us -> tau = 75e-6
      WFM transmission in EU:  50 us -> tau = 50e-6
      More info at: http://www.cliftonlaboratories.com/fm_receivers_and_de-emphasis.htm
      Simulate in octave: tau=75e-6; dt=1/48000; alpha = dt/(tau+dt); freqz([alpha],[1 -(1-alpha)])
  */
  // variables
  static int16_t last_output = 0;
  // process
  int16_t output = input_mult_alpha_r; //(fm_alpha*input_r.data) >> 15;
  output += ((32767-fm_alpha)*last_output) >> 15;
  if(input_r.valid){
    // save last & return
    last_output = output;
  }
  output_r.data = output;
  output_r.valid = input_r.valid;

  // Input reg
  input_r = input;
  input_mult_alpha_r = input_mult_alpha;

  return output_stream;
}

// Allow initial alpha multiply to be pipelined
i16_stream_t deemphasis_wfm_pipeline(i16_stream_t input){
  // Precalc as stateless pipeline input*alpha
  int16_t input_mult_alpha = (fm_alpha*input.data) >> 15;
  // Then do stateful deemph math
  return deemphasis_wfm(input, input_mult_alpha);
}