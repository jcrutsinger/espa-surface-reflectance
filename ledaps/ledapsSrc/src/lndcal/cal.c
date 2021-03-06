#include "cal.h"
#include "const.h"
#include "error.h"
#define nint(A)(A<0?(int)(A-0.5):(int)(A+0.5))

/* Functions */
/* !Revision:
 *
 * NOTES:
 * 1. TOA radiance and reflectance equations for Landsat 7 are available in
 *    http://landsathandbook.gsfc.nasa.gov/data_prod/prog_sect11_3.html
 * 2. The TOA reflectance gain/bias values from the MTL file (stored in the
 *    XML file after converting from LPGS to ESPA) do not account for the
 *    solar angle.  Thus the gain and bias need to be applied and then we
 *    still need to account for the solar angle.
 */

bool Cal(Param_t *param, Lut_t *lut, int iband, Input_t *input,
         unsigned char *line_in, int16 *line_in_sun_zen, int16 *line_out,
         unsigned char *line_out_qa, Cal_stats_t *cal_stats, int iy) {
  int is,val;
  float rad_gain, rad_bias;           /* TOA radiance gain/bias */
  float refl_gain = 0.0,
        refl_bias = 0.0;              /* TOA reflectance gain/bias */
  float rad;                          /* TOA radiance value */
  float ref_conv = 0.0;               /* TOA reflectance conversion value */
  float ref;                          /* TOA reflectance value */
  float fval;                         /* temporary float value */
  float sun_zen;                      /* solar zenith angle for the current
                                         pixel (vs. scene center) to be used
                                         with Collection products (radians) */
  int nsamp= input->size.s;
  int ifill= (int)lut->in_fill;

  /* Get the TOA radiance gain/bias */
  rad_gain = lut->meta.rad_gain[iband];
  rad_bias = lut->meta.rad_bias[iband];

  /* Get the TOA reflectance gain/bias if they are available, otherwise use
     the TOA reflectance equation from the Landsat handbook. */
  if (input->meta.use_toa_refl_consts) {
    /* Collections and some pre-Collections */
    refl_gain = lut->meta.refl_gain[iband];
    refl_bias = lut->meta.refl_bias[iband];

    if ( iy==0 ) {
      printf("*** band=%1d refl gain=%f refl bias=%f "
            "cos_sun_zen(scene center)=%f\n", iband+1, refl_gain, refl_bias,
            lut->cos_sun_zen);
      fflush(stdout);
    }
  }
  else {
    ref_conv = (PI * lut->dsun2) / (lut->esun[iband] * lut->cos_sun_zen);
  
    if ( iy==0 ) {
      printf("*** band=%1d rad gain=%f rad bias=%f dsun2=%f\n"
             "    ref_conv=%f=(PI*%f)/(%f*%f) ***\n", iband+1,
             rad_gain, rad_bias, lut->dsun2, ref_conv, lut->dsun2,
             lut->esun[iband], lut->cos_sun_zen);
      fflush(stdout);
    }
  }

  /* Loop through the samples in the line */
  for (is = 0; is < nsamp; is++) {
    val = line_in[is];
    if (val == ifill || line_out_qa[is]==lut->qa_fill ) {
      line_out[is] = lut->out_fill;
      continue;
    }

    /* flag saturated pixels, added by Feng (3/23/09) */
    if (val == SATU_VAL[iband]) {
      line_out[is] = lut->out_satu;
      continue;
    }

    fval= (float)val;

    /* If the TOA reflectance gain/bias values are available, then use them.
       Otherwise compute the TOA radiance then reflectance, per the Landsat
       handbook equations. */
    rad = (rad_gain * fval) + rad_bias;
    if (input->meta.use_toa_refl_consts && param->process_collection) {
      /* processing Collections; use per-pixel angles - convert the degree
         values to radians and then unscale */
      sun_zen = line_in_sun_zen[is] * 0.01 * RAD;
      ref = ((refl_gain * fval) + refl_bias) / cos (sun_zen);
    }
    else if (input->meta.use_toa_refl_consts) {
      /* not processing Collections so don't use per-pixel angles */
      ref = ((refl_gain * fval) + refl_bias) / lut->cos_sun_zen;
    }
    else {
      ref = rad * ref_conv;
    }

    /* Apply a scaling of 10000 (tied to the lut->scale_factor). Valid ranges
       are set up in lut.c as well. */
    line_out[is] = (int16)(ref * 10000.0 + 0.5);

    /* Cap the output using the min/max values.  Then reset the toa reflectance
       value so that it's correctly reported in the stats and the min/max
       range matches that of the image data. */
    if (line_out[is] < lut->valid_range_ref[0]) {
      line_out[is] = lut->valid_range_ref[0];
      ref = line_out[is] * 0.0001;
    }
    else if (line_out[is] > lut->valid_range_ref[1]) {
      line_out[is] = lut->valid_range_ref[1];
      ref = line_out[is] * 0.0001;
    }

#ifdef DO_STATS
    if (cal_stats->first[iband]) {
      cal_stats->idn_min[iband] = val;
      cal_stats->idn_max[iband] = val;

      cal_stats->rad_min[iband] = rad;
      cal_stats->rad_max[iband] = rad;

      cal_stats->ref_min[iband] = ref;
      cal_stats->ref_max[iband] = ref;

      cal_stats->iref_min[iband] = line_out[is];
      cal_stats->iref_max[iband] = line_out[is];

      cal_stats->first[iband] = false;
    } else {
      if (val < cal_stats->idn_min[iband]) 
        cal_stats->idn_min[iband] = val;
      if (val > cal_stats->idn_max[iband]) 
        cal_stats->idn_max[iband] = val;

      if (rad < cal_stats->rad_min[iband]) cal_stats->rad_min[iband] = rad;
      if (rad > cal_stats->rad_max[iband]) cal_stats->rad_max[iband] = rad;

      if (ref < cal_stats->ref_min[iband]) cal_stats->ref_min[iband] = ref;
      if (ref > cal_stats->ref_max[iband]) cal_stats->ref_max[iband] = ref;

      if (line_out[is] < cal_stats->iref_min[iband]) 
        cal_stats->iref_min[iband] = line_out[is];
      if (line_out[is] > cal_stats->iref_max[iband]) 
        cal_stats->iref_max[iband] = line_out[is];
    }
#endif
  }  /* end for is */

  return true;
}

bool Cal6(Lut_t *lut, Input_t *input, unsigned char *line_in, int16 *line_out,
          unsigned char *line_out_qa, Cal_stats6_t *cal_stats, int iy) {
  int is, val;
  float rad_gain, rad_bias, rad, temp;
  int nsamp= input->size_th.s;
  int ifill= (int)lut->in_fill;

  rad_gain = lut->meta.rad_gain_th;
  rad_bias = lut->meta.rad_bias_th;
  
  if ( iy==0 ) {
    printf("*** band=%1d gain=%f bias=%f ***\n", 6, rad_gain, rad_bias);
  }

  for (is = 0; is < nsamp; is++) {
    val = line_in[is];
    if (val == ifill || line_out_qa[is]==lut->qa_fill ) {
      line_out[is] = lut->out_fill;
      continue;
    }

    /* for saturated pixels */
    if (val >= SATU_VAL6) {
      line_out[is] = lut->out_satu;
      continue;
    }

    /* compute the brightness temperature in Kelvin and apply scaling of
       10.0 (tied to lut->scale_factor_th). valid ranges are set up in lut.c
       as well. */
    rad = (rad_gain * (float)val) + rad_bias;
    temp = lut->K2 / log(1.0 + (lut->K1/rad));
    line_out[is] = (int16)(temp * 10.0 + 0.5);

    /* Cap the output using the min/max values.  Then reset the temperature
       value so that it's correctly reported in the stats and the min/max
       range matches that of the image data. */
    if (line_out[is] < lut->valid_range_th[0]) {
      line_out[is] = lut->valid_range_th[0];
      temp = line_out[is] * 0.1;
    }
    else if (line_out[is] > lut->valid_range_th[1]) {
      line_out[is] = lut->valid_range_th[1];
      temp = line_out[is] * 0.1;
    }

#ifdef DO_STATS
    if (cal_stats->first) {
      cal_stats->idn_min = val;
      cal_stats->idn_max = val;

      cal_stats->rad_min = rad;
      cal_stats->rad_max = rad;

      cal_stats->temp_min = temp;
      cal_stats->temp_max = temp;

      cal_stats->itemp_min = line_out[is];
      cal_stats->itemp_max = line_out[is];

      cal_stats->first = false;
    } else {
      if (val < (int)cal_stats->idn_min) 
        cal_stats->idn_min = val;
      if (val > cal_stats->idn_max) 
        cal_stats->idn_max = val;

      if (rad < cal_stats->rad_min) cal_stats->rad_min = rad;
      if (rad > cal_stats->rad_max) cal_stats->rad_max = rad;

      if (temp < cal_stats->temp_min) cal_stats->temp_min = temp;
      if (temp > cal_stats->temp_max) cal_stats->temp_max = temp;

      if (line_out[is] < cal_stats->itemp_min) 
        cal_stats->itemp_min = line_out[is];
      if (line_out[is] > cal_stats->itemp_max) 
        cal_stats->itemp_max = line_out[is];
    }
#endif
  }  /* end for is */

  return true;
}
