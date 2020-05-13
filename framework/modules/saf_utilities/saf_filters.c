/*
 * Copyright 2019 Leo McCormack
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file: saf_filters.c
 * @brief Contains a collection of filter design equations
 *
 * @author Leo McCormack
 * @date 01.03.2019
 */

#include "saf_filters.h" 
#include "saf_utilities.h"

/**
 * Applies a windowing function (see WINDOWING_FUNCTION_TYPES enum) of length
 * 'winlength', to vector 'x'.
 */
static void applyWindowingFunction
(
    WINDOWING_FUNCTION_TYPES type,
    int winlength,
    float* x
)
{
    int i, N;
    
    /* if winlength is odd -> symmetric window (mid index has value=1) */
    if ( !(winlength % 2 == 0) )
        N = winlength-1;
    /* otherwise, if winlength is even (index: winlength/2+1 = 1.0, but first
     * value != last value) */
    else
        N = winlength;
    
    switch(type){
        case WINDOWING_FUNCTION_RECTANGULAR:
            break;
            
        case WINDOWING_FUNCTION_HAMMING:
            for(i=0; i<winlength; i++)
                x[i] *= 0.54f - 0.46f * (cosf(2.0f*M_PI*(float)i/(float)N)); /* more wide-spread coefficient values */
            /* optimal equiripple coefficient values: */
            /*x[i] *= 0.53836f - 0.46164f * (cosf(2.0f*M_PI*(float)i/(float)N));*/
            break;
            
        case WINDOWING_FUNCTION_HANN:
            for(i=0; i<winlength; i++)
                x[i] *= 0.5f - 0.5f * (cosf(2.0f*M_PI*(float)i/(float)N));
            break;
            
        case WINDOWING_FUNCTION_BARTLETT:
            for(i=0; i<winlength; i++)
                x[i] *= 1.0f - 2.0f * fabsf((float)i-((float)N/2.0f))/(float)N;
            break;
            
        case WINDOWING_FUNCTION_BLACKMAN:
            for(i=0; i<winlength; i++){
                x[i] *= 0.42659f -
                        0.49656f *cosf(2.0f*M_PI*(float)i/(float)N) +
                        0.076849f*cosf(4.0f*M_PI*(float)i/(float)N);
            }
            break;
            
        case WINDOWING_FUNCTION_NUTTALL:
            for(i=0; i<winlength; i++){
                x[i] *= 0.355768f -
                        0.487396f*cosf(2.0f*M_PI*(float)i/(float)N) +
                        0.144232f*cosf(4.0f*M_PI*(float)i/(float)N) -
                        0.012604f*cosf(6.0f*M_PI*(float)i/(float)N);
            }
            break;
            
        case WINDOWING_FUNCTION_BLACKMAN_NUTTALL:
            for(i=0; i<winlength; i++){
                x[i] *= 0.3635819f -
                        0.4891775f*cosf(2.0f*M_PI*(float)i/(float)N) +
                        0.1365995f*cosf(4.0f*M_PI*(float)i/(float)N) +
                        0.0106411f*cosf(4.0f*M_PI*(float)i/(float)N);
            }
            break;
            
        case WINDOWING_FUNCTION_BLACKMAN_HARRIS:
            for(i=0; i<winlength; i++){
                x[i] *= 0.35875f -
                        0.48829f*cosf(2.0f*M_PI*(float)i/(float)N) +
                        0.14128f*cosf(4.0f*M_PI*(float)i/(float)N) +
                        0.01168f*cosf(4.0f*M_PI*(float)i/(float)N);
            }
            break;
    }
}

void getWindowingFunction
(
    WINDOWING_FUNCTION_TYPES type,
    int winlength,
    float* win
)
{
    int i;
    
    for(i=0; i<winlength; i++)
        win[i] = 1.0f;
    applyWindowingFunction(type, winlength, win);
}

void getOctaveBandCutoffFreqs
(
    float* centreFreqs,  /* nCutoffFreq x 1 */
    int nCentreFreqs,
    float* cutoffFreqs   /* (nCutoffFreq+1) x 1 */
)
{
    int i;
    for(i=0; i<nCentreFreqs-1; i++)
        cutoffFreqs[i] = 2.0f*centreFreqs[i]/sqrtf(2.0f);
}

// TODO:
//    mid of log
//    exp(1)^((log(400)+log(2000))/2)

void flattenMinphase
(
    float* x,
    int len
)
{
    int i;
    float_complex* ctd_tmp, *tdi_f, *tdi_f_labs, *dt_min_f;
    void* hFFT;
    
    /* prep */
    ctd_tmp = malloc1d(len*sizeof(float_complex));
    tdi_f = malloc1d(len*sizeof(float_complex));
    tdi_f_labs = malloc1d(len*sizeof(float_complex));
    dt_min_f = malloc1d(len*sizeof(float_complex));
    saf_fft_create(&hFFT, len);
    
    /* fft */
    for(i=0; i<len; i++)
        ctd_tmp[i] = cmplxf(x[i], 0.0f);
    saf_fft_forward(hFFT, (float_complex*)ctd_tmp, (float_complex*)tdi_f);
    
    /* take log(cabs()) */
    for(i=0; i<len; i++)
        tdi_f_labs[i] = cmplxf(logf(cabsf(tdi_f[i])), 0.0f);
    
    /* Hilbert to acquire discrete-time analytic signal */
    hilbert(tdi_f_labs, len, dt_min_f);
    
    /* compute minimum-phase response, and apply to tdi_f to flatten it to unity magnitude */
    for(i=0; i<len; i++)
        dt_min_f[i] = ccdivf(tdi_f[i], cexpf(conjf(dt_min_f[i])));
    
    /* ifft */
    saf_fft_backward(hFFT, dt_min_f, ctd_tmp);
    
    /* overwrite input with EQ'd version */
    for(i=0; i<len; i++)
        x[i] = crealf(ctd_tmp[i]);
    
    /* tidy up */
    saf_fft_destroy(&hFFT);
    free(ctd_tmp);
    free(tdi_f);
    free(tdi_f_labs);
    free(dt_min_f);
}

void biQuadCoeffs
(
    BIQUAD_FILTER_TYPES filterType,  
    float fc,
    float fs,
    float Q,
    float gain_dB,
    float b[3],
    float a[3] 
)
{
    float K, KK, D, V0;
    
    a[0] = 1.0f;
    
    /* calculate the IIR filter coefficients */
    switch (filterType){
        case BIQUAD_FILTER_LPF:
            /* Filter design equations - DAFX (2nd ed) p50 */
            K = tanf(M_PI * fc/fs);
            KK = K * K;
            D = KK * Q + K + Q;
            b[0] = (KK * Q)/D;
            b[1] = (2.0f * KK * Q)/D;
            b[2] = b[0];
            a[1] = (2.0f * Q * (KK - 1.0f))/D;
            a[2] = (KK * Q - K + Q)/D;
            break;
            
        case BIQUAD_FILTER_HPF:
            /* Filter design equations - DAFX (2nd ed) p50 */
            K = tanf(M_PI * fc/fs);
            KK = K * K;
            D = KK * Q + K + Q;
            b[0] = (Q)/D;
            b[1] = -(2.0f * Q)/D;
            b[2] = b[0];
            a[1] = (2.0f * Q * (KK - 1.0f))/D;
            a[2] = (KK * Q - K + Q)/D;
            break;
            
        case BIQUAD_FILTER_LOW_SHELF:
            /* Filter design equations - DAFX (2nd ed) p64 */
            K = tanf(M_PI * fc/fs);
            V0 = powf(10.0f, (gain_dB/20.0f));
            if (V0 < 1.0f)
                V0 = 1.0f/V0;
            KK = K * K;
            if (gain_dB > 0.0f){
                D = 1.0f + sqrtf(2.0f) * K + KK;
                b[0] = (1.0f + sqrtf(2.0f * V0) * K + V0 * KK)/D;
                b[1] = (2.0f*(V0*KK - 1.0f))/D;
                b[2] = (1.0f - sqrtf(2.0f * V0) * K + V0 * KK)/D;
                a[1] = (2.0f * (KK - 1.0f))/D;
                a[2] = (1.0f - sqrtf(2.0f) * K + KK)/D;
            }
            else{
                D = V0 + sqrtf(2.0f*V0)*K + KK;
                b[0] = (V0*(1.0f + sqrtf(2.0f)*K + KK))/D;
                b[1] = (2.0f*V0*(KK - 1.0f))/D;
                b[2] = (V0*(1.0f - sqrtf(2.0f)*K + KK))/D;
                a[1] = (2.0f * (KK - V0))/D;
                a[2] = (V0 - sqrtf(2.0f*V0)*K + KK)/D;
            }
            break;
            
        case BIQUAD_FILTER_HI_SHELF:
            /* Filter design equations - DAFX (2nd ed) p64 */
            K = tanf(M_PI * fc/fs);
            V0 = powf(10.0f, (gain_dB/20.0f));
            if (V0 < 1.0f)
                V0 = 1.0f/V0;
            KK = K * K;
            if (gain_dB > 0.0f){
                D = 1.0f + sqrtf(2.0f) * K + KK;
                b[0] = (V0 + sqrtf(2.0f * V0) * K + KK)/D;
                b[1] = (2.0f*(KK - V0))/D;
                b[2] = (V0 - sqrtf(2.0f * V0) * K + KK)/D;
                a[1] = (2.0f*(KK - 1.0f))/D;
                a[2] = (1.0f - sqrtf(2.0f) * K + KK)/D;
            }
            else{
                D = 1.0f + sqrtf(2.0f*V0) * K + V0*KK;
                b[0] = (V0*(1.0f + sqrtf(2.0f)*K + KK))/D;
                b[1] = (2.0f*V0*(KK - 1.0f))/D;
                b[2] = (V0*(1.0f - sqrtf(2.0f)*K + KK))/D;
                a[1] = (2.0f * (V0*KK - 1.0f))/D;
                a[2] = (1.0f - sqrtf(2.0f*V0)*K + V0*KK)/D;
            }
            break;
            
        case BIQUAD_FILTER_PEAK:
            /* Filter design equations - DAFX (2nd ed) p66 */
            K = tanf(M_PI * fc/fs);
            V0 = powf(10.0f, (gain_dB/20.0f));
            KK = K * K;
            if (gain_dB > 0.0f){
                D = 1.0f + (K/Q) + KK;
                b[0] = (1.0f + (V0/Q) * K + KK)/D;
                b[1] = (2.0f*(KK - 1.0f))/D;
                b[2] = (1.0f - (V0/Q) * K + KK)/D;
                a[1] = b[1];
                a[2] = (1.0f - (K/Q) + KK)/D;
            }
            else {
                D = 1.0f + (K/(V0*Q)) + KK;
                b[0] = (1.0f + (K/Q) + KK)/D;
                b[1] = (2.0f*(KK - 1.0f))/D;
                b[2] = (1.0f - (K/Q) + KK)/D;
                a[1] = b[1];
                a[2] = (1.0f - (K/(V0*Q)) + KK)/D;
            }
            break;
    }
}

void applyBiQuadFilter
(
     float b[3],
     float a[3],
     float w_z_12[2],
     float* signal,
     int nSamples
)
{
    int n;
    float wn;
    
    /* biquad difference equation (Direct form 2) */
    for(n=0; n<nSamples; n++){
        wn = signal[n] - a[1] * w_z_12[0] - a[2] * w_z_12[1];
        signal[n] = b[0] * wn + b[1] * w_z_12[0] + b[2] * w_z_12[1];
        /* shuffle delays */
        w_z_12[1] = w_z_12[0];
        w_z_12[0] = wn;
    }
}

void evalBiQuadTransferFunction
(
    float b[3],
    float a[3],
    float* freqs,
    int nFreqs,
    float fs,
    float* magnitude_dB,
    float* phase_rad
)
{
    int ff;
    float w, denom_real, denom_imag, num_real, num_imag;
    
    for(ff=0; ff<nFreqs; ff++){
        w = tanf(M_PI * freqs[ff]/fs);
        /* substituting Euler, z = e^(-jwn) = cos(wn) + j*sin(wn), into:
         * H(z) = (b0 + b1*z^(-1) + b2*z^(-2)) / (1 + a1*z^(-1) + a2*z^(-2)): */
        denom_real = 1.0f + a[1]*cosf(w) + a[2]*cosf(2.0f*w);
        denom_imag = a[1]*sinf(w) + a[2]*sinf(2*w);
        num_real = b[0] + b[1]*cosf(w) + b[2]*cosf(2.0f*w);
        num_imag = b[1]*sinf(w) + b[2]*sinf(2.0f*w);
        
        if(magnitude_dB!=NULL){
            magnitude_dB[ff] = sqrtf( (powf(num_real, 2.0f) + powf(num_imag, 2.0f)) / (powf(denom_real, 2.0f) + powf(denom_imag, 2.0f)) );
            magnitude_dB[ff] = 20.0f*log10f(magnitude_dB[ff]);
        }
        if(phase_rad!=NULL)
            phase_rad[ff] = atan2f(num_imag,num_real) - atan2f(denom_imag, denom_real);
    }
}

void FIRCoeffs
(
    FIR_FILTER_TYPES filterType,
    int order,
    float fc1,
    float fc2, /* only needed for band-pass/stop */
    float fs,
    WINDOWING_FUNCTION_TYPES windowType,
    int scalingFLAG,
    float* h_filt
)
{
    int i, h_len;
    float ft1, ft2, h_sum, f0;
    float_complex h_z_sum;
    
    h_len = order + 1;
    ft1 = fc1/(fs*2.0f);
    
    /* compute filter weights */
    if(order % 2 == 0){
        /* if order is multiple of 2 */
        switch(filterType){
            case FIR_FILTER_LPF:
                for(i=0; i<h_len; i++)
                    h_filt[i] = i==order/2 ? 2.0f*ft1 : sinf(2.0f*M_PI*ft1*(float)(i-order/2)) / (M_PI*(float)(i-order/2));
                break;
                
            case FIR_FILTER_HPF:
                for(i=0; i<h_len; i++)
                    h_filt[i] = i==order/2 ? 1.0f - 2.0f*ft1 : -sinf(2.0f*ft1*M_PI*(float)(i-order/2)) / (M_PI*(float)(i-order/2));
                break;
                
            case FIR_FILTER_BPF:
                ft2 = fc2/(fs*2.0f);
                for(i=0; i<h_len; i++){
                    h_filt[i] = i==order/2 ? 2.0f*(ft2-ft1) :
                        sinf(2.0f*M_PI*ft2*(float)(i-order/2)) / (M_PI*(float)(i-order/2)) - sinf(2.0f*M_PI*ft1*(float)(i-order/2)) / (M_PI*(float)(i-order/2));
                }
                break;
                
            case FIR_FILTER_BSF:
                ft2 = fc2/(fs*2.0f);
                for(i=0; i<h_len; i++){
                    h_filt[i] = i==order/2 ? 1.0f - 2.0f*(ft2-ft1) :
                        sinf(2.0f*M_PI*ft1*(float)(i-order/2)) / (M_PI*(float)(i-order/2)) - sinf(2.0f*M_PI*ft2*(float)(i-order/2)) / (M_PI*(float)(i-order/2));
                }
                break;
        }
    }
    else
        assert(0); /* please specify an even value for the filter 'order' argument */
    
    /* Apply windowing function */
    applyWindowingFunction(windowType, h_len, h_filt);
    
    /* Scaling, to ensure pass-band is truely at 1 (0dB).
     * [1] "Programs for Digital Signal Processing", IEEE Press John Wiley &
     *     Sons, 1979, pg. 5.2-1.
     */
    if(scalingFLAG){ 
        switch(filterType){
            case FIR_FILTER_LPF:
            case FIR_FILTER_BSF:
                h_sum = 0.0f;
                for(i=0; i<h_len; i++)
                    h_sum += h_filt[i];
                for(i=0; i<h_len; i++)
                    h_filt[i] /= h_sum;
                break;
                
            case FIR_FILTER_HPF:
                f0 = 1.0f;
                h_z_sum = cmplxf(0.0f, 0.0f);
                for(i=0; i<h_len; i++)
                    h_z_sum = ccaddf(h_z_sum, crmulf(cexpf(cmplxf(0.0f, -2.0f*M_PI*(float)i*f0/2.0f)), h_filt[i]));
                h_sum = cabsf(h_z_sum);
                for(i=0; i<h_len; i++)
                    h_filt[i] /= h_sum;
                break;
                
            case FIR_FILTER_BPF:
                f0 = (fc1/fs+fc2/fs)/2.0f;
                h_z_sum = cmplxf(0.0f, 0.0f);
                for(i=0; i<h_len; i++)
                    h_z_sum = ccaddf(h_z_sum, crmulf(cexpf(cmplxf(0.0f, -2.0f*M_PI*(float)i*f0/2.0f)), h_filt[i]));
                h_sum = cabsf(h_z_sum);
                for(i=0; i<h_len; i++)
                    h_filt[i] /= h_sum;
                break;
        }
    }
}

void FIRFilterbank
(
    int order,
    float* fc,  /* cut-off frequencies; nCutoffFreq x 1 */
    int nCutoffFreq,
    float sampleRate,
    WINDOWING_FUNCTION_TYPES windowType,
    int scalingFLAG,
    float* filterbank /* (nCutoffFreq+1) x (order+1) */
)
{
    int k, nFilt;
    
    /* Number of filters returned is always one more than the number of cut-off frequencies */
    nFilt = nCutoffFreq + 1;
    
    /* first and last bands are low-pass and high pass filters, using the first
     * and last cut-off frequencies in vector 'fc', respectively.  */
    FIRCoeffs(FIR_FILTER_LPF, order, fc[0], 0.0f, sampleRate, windowType, scalingFLAG, filterbank);
    FIRCoeffs(FIR_FILTER_HPF, order, fc[nCutoffFreq-1], 0.0f, sampleRate, windowType, scalingFLAG, &filterbank[(nFilt-1)*(order+1)]);
    
    /* the inbetween bands are then band-pass filters: */
    if(nCutoffFreq>1){
        for(k=1; k<nFilt-1; k++)
            FIRCoeffs(FIR_FILTER_BPF, order, fc[k-1], fc[k], sampleRate, windowType, scalingFLAG, &filterbank[k*(order+1)]);
    }
}

void butterCoeffs
(
    BUTTER_FILTER_TYPES filterType,
    int order,
    float cutoff1,
    float cutoff2,
    float sampleRate,
    double* b_coeffs,
    double* a_coeffs
)
{
    int i, j, k, np, tmp_len, numStates, oddPolesFLAG, nCoeffs;
    double wlow, whi, w0, wl, w1, BW, Wn1, q;
    double den[3];
    double* c_state, *r, *b_coeffs_real;
    double** a_state, **bf_ss, **tmp1, **tmp2, **a_bili;
    double_complex kaT, kbT;
    double_complex den_cmplx[3];
    double_complex* proto, *proto_tmp, *a_coeffs_cmplx, *kern, *rcmplx, *b_coeffs_cmplx;

    wlow = (double)cutoff1/((double)sampleRate/2.0);
    whi = (double)cutoff2/((double)sampleRate/2.0);
    w0 = 4.0 * tan(M_PI*wlow/2.0);
    Wn1 = 0.0;

    /* Compute prototype for Nth order Butterworth analogue lowpass filter */
    if (order%2 != 0){/* ISODD */
        tmp_len = (int)((float)order/2.0f); /* floor */
        np = 2*tmp_len+1;
        proto = malloc1d(np*sizeof(double_complex));
        proto[np-1] = cmplx(-1.0,0.0);
    }
    else{ /* ISEVEN */
        tmp_len = order/2;
        np = 2*tmp_len;
        proto = malloc1d(np*sizeof(double_complex));
    }
    proto_tmp = malloc1d(np*sizeof(double_complex));
    for(i=1, j=0; i<=order-1; i+=2, j++)
        proto_tmp[j] = cexp(cmplx(0.0, M_PI*(double)i/(2.0*(double)order) + M_PI/2.0) );
    for (i=0; i<tmp_len; i++){
        proto[2*i] = proto_tmp[i];
        proto[2*i+1] = conj(proto_tmp[i]);
    }

    /* Transform prototype into state space  */
    numStates = np;
    cmplxPairUp(proto, proto_tmp, np);
    memcpy(proto, proto_tmp, np*sizeof(double_complex));
    free(proto_tmp);
    a_state = (double**)calloc2d(numStates,numStates,sizeof(double));
    c_state = malloc1d(numStates*sizeof(double));
    if (np%2 != 0){/* ISODD */
        a_state[0][0] = creal(proto[np-1]);
        c_state[0] = 1.0;
        np--;
        oddPolesFLAG = 1;
    }
    else
        oddPolesFLAG = 0;

    /* Adjust indices as needed */
    for(i=1; i<np; i+=2){
        polyz_v(&proto[i-1], den_cmplx, 2);
        for(j=0; j<3; j++)
            den[j] = creal(den_cmplx[j]);
        j = oddPolesFLAG ? i-1 : i-2;

        if(j==-1){
            a_state[0][0] = -den[1];
            a_state[0][1] = -den[2];
            a_state[1][0] = 1.0;
            a_state[1][1] = 0.0;
            c_state[0] = 0.0;
            c_state[1] = 1.0;
        }
        else{
            for(k=0; k<j+1; k++)
                a_state[j+1][k] = c_state[k];
            a_state[j+1][j+1] = -den[1];
            a_state[j+1][j+2] = -den[2];
            a_state[j+2][j+1] = 1.0;
            a_state[j+2][j+2] = 0.0;

            for(k=0; k<j+1; k++)
                c_state[k] = 0.0;
            c_state[j+1] = 0.0;
            c_state[j+2] = 1.0;
        }
    }

    /* Transform lowpass filter into the desired filter (while in state space) */
    switch(filterType){
        case BUTTER_FILTER_HPF:
            utility_dinv(ADR2D(a_state), ADR2D(a_state), numStates);
        case BUTTER_FILTER_LPF:
            bf_ss = (double**)malloc2d(numStates,numStates,sizeof(double));
            for(i=0; i<numStates; i++)
                for(j=0; j<numStates; j++)
                    bf_ss[i][j] = w0*(a_state[i][j]);
            break;
        case BUTTER_FILTER_BSF:
            utility_dinv(ADR2D(a_state), ADR2D(a_state), numStates);
        case BUTTER_FILTER_BPF:
            numStates = numStates*2;
            w1 = 4.0*tan(M_PI*whi/2.0);
            BW = w1 - w0;
            Wn1 = sqrt(w0*w1);
            q = Wn1/BW;
            bf_ss = (double**)calloc2d(numStates,numStates,sizeof(double));
            for(i=0; i<numStates/2; i++)
                for(j=0; j<numStates/2; j++)
                    bf_ss[i][j] = Wn1 * (a_state[i][j]) /q;
            for(i=numStates/2; i<numStates; i++)
                for(j=0; j<numStates/2; j++)
                    bf_ss[i][j] = (i-numStates/2) == j ? -Wn1 : 0.0;
            for(i=0; i<numStates/2; i++)
                for(j=numStates/2; j<numStates; j++)
                    bf_ss[i][j] = i == (j-numStates/2) ? Wn1 : 0.0;
            break;
    }
    nCoeffs = numStates+1;

    /* Bilinear transformation to find the discrete equivalent of the filter */
    tmp1 = (double**)malloc2d(numStates,numStates,sizeof(double));
    tmp2 = (double**)malloc2d(numStates,numStates,sizeof(double));
    a_bili = (double**)malloc2d(numStates,numStates,sizeof(double));
    for(i=0; i<numStates; i++){
        for(j=0; j<numStates; j++){
            tmp1[i][j] = (i==j ? 1.0f : 0.0f) + bf_ss[i][j]*0.25;
            tmp2[i][j] = (i==j ? 1.0f : 0.0f) - bf_ss[i][j]*0.25;
        }
    }
    utility_dglslv(ADR2D(tmp2), numStates, ADR2D(tmp1), numStates, ADR2D(a_bili));

    /* Compute the filter coefficients for the numerator and denominator */
    a_coeffs_cmplx = malloc1d(nCoeffs*sizeof(double_complex));
    polyd_m(ADR2D(a_bili), a_coeffs_cmplx, numStates);
    rcmplx = NULL;
    r = NULL;
    switch(filterType){
        case BUTTER_FILTER_LPF:
            r = malloc1d(numStates*sizeof(double));
            for(i=0; i<numStates; i++)
                r[i] = -1.0;
            wl = 0.0;
            break;
        case BUTTER_FILTER_HPF:
            r = malloc1d(numStates*sizeof(double));
            for(i=0; i<numStates; i++)
                r[i] = 1.0;
            wl = M_PI;
            break;
        case BUTTER_FILTER_BPF:
            r = malloc1d(numStates*sizeof(double));
            wl = 2.0*atan2(Wn1, 4.0);
            for(i=0; i<order;i++)
                r[i] = 1.0;
            for(; i<2*order;i++)
                r[i] = -1.0;
            break;
        case BUTTER_FILTER_BSF:
            rcmplx = malloc1d(numStates*sizeof(double_complex));
            Wn1 = 2.0*atan2(Wn1,4.0);
            wl = 0.0;
            for(i=0; i<numStates;i++)
                rcmplx[i] = cexp(cmplx(0.0, Wn1*pow(-1.0,(double)i)));
            break;
    }
    b_coeffs_real = malloc1d(nCoeffs*sizeof(double));
    if(filterType == BUTTER_FILTER_BSF){
        b_coeffs_cmplx = malloc1d(nCoeffs*sizeof(double_complex));
        polyz_v(rcmplx, b_coeffs_cmplx, numStates);
        for(i=0; i<nCoeffs; i++)
            b_coeffs_real[i] = creal(b_coeffs_cmplx[i]);
        free(b_coeffs_cmplx);
    }
    else
        polyd_v(r, b_coeffs_real, numStates);
    kern = calloc1d(nCoeffs,sizeof(double_complex));
    kaT = cmplx(0.0,0.0);
    kbT = cmplx(0.0,0.0);
    for(i=0; i<nCoeffs; i++){
        kern[i] = cexp(cmplx(0.0,-wl*(double)i));
        kaT = ccadd(kaT, crmul(kern[i],creal(a_coeffs_cmplx[i])));
        kbT = ccadd(kbT, crmul(kern[i],b_coeffs_real[i]));
    }

    /* output */
    for(i=0; i<nCoeffs; i++){
        b_coeffs[i] = creal(crmul(ccdiv(kaT,kbT), b_coeffs_real[i]));
        a_coeffs[i] = creal(a_coeffs_cmplx[i]);
    }

    /* clean-up */
    free(proto);
    free(a_state);
    free(c_state);
    free(bf_ss);
    free(tmp1);
    free(tmp2);
    free(a_bili);
    free(a_coeffs_cmplx);
    free(b_coeffs_real);
    free(kern);
}

typedef struct _faf_IIRFB_data{
    int nBands;
    float** b_lpf, **a_lpf, **b_hpf, **a_hpf;

}faf_IIRFB_data;

void faf_IIRFilterbank_create
(
    void** hFaF,
    int order, // ENUM 1st or 3rd order only
    float* fc,
    int nCutoffFreq,
    float sampleRate
)
{
    *hFaF = malloc1d(sizeof(faf_IIRFB_data));
    faf_IIRFB_data *fb = (faf_IIRFB_data*)(*hFaF);
    double b_lpf[4], a_lpf[4], b_hpf[4], a_hpf[4], r[7], revb[4], reva[4], q[4];
    double tmp[7], tmp2[7];
    double_complex d1[3], d2[3], d1_num[3], d2_num[3];
    double_complex z[3], A[3][3], ztmp[7], ztmp2[7];
    int i, j, f, filtLen, d1_len, d2_len;

    assert(nCutoffFreq>=1);

    filtLen = order + 1;

    /* Number of filters returned is always one more than the number of cut-off frequencies */
    fb->nBands = nCutoffFreq + 1;

    for(f=0; f<nCutoffFreq; f++){

        /* Low-pass filter */
        butterCoeffs(BUTTER_FILTER_LPF, order, fc[f], 0.0f, sampleRate, (double*)b_lpf, (double*)a_lpf);

        /* IIR power complementary filter design (i.e. High-pass) */
        for(i=0; i<filtLen; i++){
            reva[i] = a_lpf[filtLen-i-1];
            revb[i] = b_lpf[filtLen-i-1];
        }
        convd(revb, b_lpf, filtLen, filtLen, tmp);
        convd(a_lpf, reva, filtLen, filtLen, tmp2);
        for(i=0; i<2*filtLen-1; i++)
            r[i] = tmp[i] - tmp2[i];
        q[0] = sqrt(-r[0]/-1.0);
        q[1] = -r[1]/(2.0*-1.0*q[0]);
        if(order==3){
            q[3]=conj(-1.0*q[0]);
            q[2]=conj(-1.0*q[1]);
        }
        for(i=0; i<filtLen; i++)
            q[i] =  b_lpf[i] - q[i];

        /* Find roots of polynomial  */
        if(order==1)
            z[0] = cmplx(-q[1]/q[0], 0.0);
        else{ /* 3rd order */
            memset(A, 0, 9*sizeof(double_complex));
            A[0][0] = cmplx(-q[1]/q[0], 0.0);
            A[0][1] = cmplx(-q[2]/q[0], 0.0);
            A[0][2] = cmplx(-q[3]/q[0], 0.0);
            A[1][0] = cmplx(1.0, 0.0);
            A[2][1] = cmplx(1.0, 0.0);
            utility_zeig((double_complex*)A, 3, NULL, NULL, NULL, (double_complex*)z);
        }

        /* Separate the zeros inside the unit circle and the ones outside to
         * form the allpass functions */
        d1[0] = cmplx(1.0, 0.0);
        d2[0] = cmplx(1.0, 0.0);
        d1_len = d2_len = 1;
        for(i=0; i<order; i++){
            if (cabs(z[i]) < 1.0){
                ztmp[0] = cmplx(1.0, 0.0);
                ztmp[1] = -z[i];
                convz(d2,ztmp,d2_len,2,ztmp2);
                d2_len++;
                for(j=0; j<d2_len; j++)
                    d2[j] = ztmp2[j];
            }
            else{
                ztmp[0] = cmplx(1.0, 0.0);
                ztmp[1] = ccdiv(-1.0, conj(z[i]));
                convz(d1,ztmp,d1_len,2,ztmp2);
                d1_len++;
                for(j=0; j<d1_len; j++)
                    d1[j] = ztmp2[j];
            }
        }

        /* Convert coupled allpass filter to transfer function form
         * (code from https://github.com/nsk1001/Scilab-functions written by
         * Nagma Samreen Khan.) */
        for(i=0; i<d1_len; i++)
            d1_num[i] = conj(d1[d1_len-i-1]);
        for(i=0; i<d2_len; i++)
            d2_num[i] = conj(d2[d2_len-i-1]);

        convz(d1_num, d2, d1_len, d2_len, ztmp);
        convz(d2_num, d1, d2_len, d1_len, ztmp2);
        for(i=0; i<filtLen; i++){
            b_hpf[i] = -0.5 * creal(ccsub(ztmp[filtLen-i-1], ztmp2[filtLen-i-1]));
            a_hpf[i] = b_lpf[i];
        }

    }
}

