/*
 * Copyright 2017-2018 Leo McCormack
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
 * @file array2sh.c
 * @brief Spatially encodes spherical or cylindrical sensor array signals into
 *        spherical harmonic signals utilising theoretical encoding filters.
 *
 * The algorithms within array2sh were pieced together and developed in
 * collaboration with Symeon Delikaris-Manias and Angelo Farina.
 * A detailed explanation of the algorithms within array2sh can be found in [1].
 * Also included, is a diffuse-field equalisation option for frequencies past
 * aliasing, developed in collaboration with Archontis Politis, 8.02.2019
 *
 * @note Since the algorithms are based on theory, only array designs where
 *       there are analytical solutions available are supported. i.e. only
 *       spherical or cylindrical arrays, which have phase-matched sensors.
 *
 * @see [1] McCormack, L., Delikaris-Manias, S., Farina, A., Pinardi, D., and
 *          Pulkki, V., "Real-time conversion of sensor array signals into
 *          spherical harmonic signals with applications to spatially localised
 *          sub-band sound-field analysis," in Audio Engineering Society
 *          Convention 144, Audio Engineering Society, 2018.
 *
 * @author Leo McCormack
 * @date 13.09.2017
 */

#include "array2sh_internal.h" 

void array2sh_create
(
    void ** const phA2sh
)
{
    array2sh_data* pData = (array2sh_data*)malloc1d(sizeof(array2sh_data));
    *phA2sh = (void*)pData;
    int ch;
     
    /* defualt parameters */
    array2sh_createArray(&(pData->arraySpecs)); 
    pData->filterType = FILTER_TIKHONOV;
    pData->regPar = 15.0f;
    pData->chOrdering = CH_ACN;
    pData->norm = NORM_SN3D;
    pData->c = 343.0f;
    pData->gain_dB = 0.0f; /* post-gain */ 
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    array2sh_initArray(arraySpecs, MICROPHONE_ARRAY_PRESET_DEFAULT, &(pData->order), 1);
    pData->enableDiffEQpastAliasing = 1;
    
    /* time-frequency transform + buffers */
    pData->hSTFT = NULL;
    pData->STFTInputFrameTF = (complexVector*)malloc1d(MAX_NUM_SENSORS * sizeof(complexVector));
    for(ch=0; ch< MAX_NUM_SENSORS; ch++) {
        pData->STFTInputFrameTF[ch].re = (float*)calloc1d(HYBRID_BANDS, sizeof(float));
        pData->STFTInputFrameTF[ch].im = (float*)calloc1d(HYBRID_BANDS, sizeof(float));
    }
    pData->STFTOutputFrameTF = (complexVector*)malloc1d(MAX_NUM_SH_SIGNALS * sizeof(complexVector));
    for(ch=0; ch< MAX_NUM_SH_SIGNALS; ch++) {
        pData->STFTOutputFrameTF[ch].re = (float*)calloc1d(HYBRID_BANDS, sizeof(float));
        pData->STFTOutputFrameTF[ch].im = (float*)calloc1d(HYBRID_BANDS, sizeof(float));
    }
    pData->tempHopFrameTD_in = (float**)malloc2d( MAX(MAX_NUM_SH_SIGNALS, MAX_NUM_SENSORS), HOP_SIZE, sizeof(float));
    pData->tempHopFrameTD_out = (float**)malloc2d( MAX(MAX_NUM_SH_SIGNALS, MAX_NUM_SENSORS), HOP_SIZE, sizeof(float));
    
    /* internal */
    pData->progressBar0_1 = 0.0f;
    pData->progressBarText = malloc1d(ARRAY2SH_PROGRESSBARTEXT_CHAR_LENGTH*sizeof(char));
    strcpy(pData->progressBarText,"");
    pData->evalStatus = EVAL_STATUS_NOT_EVALUATED;
    pData->evalRequestedFLAG = 0;
    pData->reinitSHTmatrixFLAG = 1;
    pData->new_order = pData->order;
    pData->bN = NULL;
    
    /* display related stuff */
    pData->bN_modal_dB = (float**)malloc2d(HYBRID_BANDS, MAX_SH_ORDER + 1, sizeof(float));
    pData->bN_inv_dB = (float**)malloc2d(HYBRID_BANDS, MAX_SH_ORDER + 1, sizeof(float));
    pData->cSH = (float*)calloc1d((HYBRID_BANDS)*(MAX_SH_ORDER + 1),sizeof(float));
    pData->lSH = (float*)calloc1d((HYBRID_BANDS)*(MAX_SH_ORDER + 1),sizeof(float));

    /* reset FIFO buffers */
    pData->FIFO_idx = 0;
    memset(pData->inFIFO, 0, MAX_NUM_SENSORS*FRAME_SIZE*sizeof(float));
    memset(pData->outFIFO, 0, MAX_NUM_SH_SIGNALS*FRAME_SIZE*sizeof(float));
}

void array2sh_destroy
(
    void ** const phM2sh
)
{
    array2sh_data *pData = (array2sh_data*)(*phM2sh);
    int ch;

    if (pData != NULL) {
        /* not safe to free memory during evaluation */
        while (pData->evalStatus == EVAL_STATUS_EVALUATING)
            SAF_SLEEP(10);
        
        /* free afSTFT and buffers */
        if (pData->hSTFT != NULL)
            afSTFTfree(pData->hSTFT);
        for (ch = 0; ch< MAX_NUM_SENSORS; ch++) {
            free(pData->STFTInputFrameTF[ch].re);
            free(pData->STFTInputFrameTF[ch].im);
        }
        for(ch=0; ch< MAX_NUM_SH_SIGNALS; ch++) {
            free(pData->STFTOutputFrameTF[ch].re);
            free(pData->STFTOutputFrameTF[ch].im);
        }
        free(pData->STFTOutputFrameTF);
        free(pData->STFTInputFrameTF);
        free(pData->tempHopFrameTD_in);
        free(pData->tempHopFrameTD_out);
        array2sh_destroyArray(&(pData->arraySpecs));
        
        /* Display stuff */
        free((void**)pData->bN_modal_dB);
        free((void**)pData->bN_inv_dB);
        
        free(pData->progressBarText);
        
        free(pData);
        pData = NULL;
    }
}

void array2sh_init
(
    void * const hA2sh,
    int          sampleRate
)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    int band;
    
    pData->fs = sampleRate;
    for(band=0; band <HYBRID_BANDS; band++){
        if(sampleRate==44100)
            pData->freqVector[band] =  (float)__afCenterFreq44100[band];
        else /* assume 48e3 */
            pData->freqVector[band] =  (float)__afCenterFreq48e3[band];
    } 
    pData->freqVector[0] = pData->freqVector[1]/4.0f; /* avoids NaNs at DC */
}

void array2sh_evalEncoder
(
    void* const hA2sh
)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    
    if (pData->evalStatus != EVAL_STATUS_NOT_EVALUATED)
        return; /* eval not required */
    
    /* for progress bar */
    pData->evalStatus = EVAL_STATUS_EVALUATING;
    strcpy(pData->progressBarText,"Initialising evaluation");
    pData->progressBar0_1 = 0.0f;
    
    /* Evaluate Encoder */
    array2sh_evaluateSHTfilters(hA2sh);
    
    /* done! */
    strcpy(pData->progressBarText,"Done!");
    pData->progressBar0_1 = 1.0f; 
    pData->evalStatus = EVAL_STATUS_RECENTLY_EVALUATED;
}

void array2sh_process
(
    void  *  const hA2sh,
    float ** const inputs,
    float ** const outputs,
    int            nInputs,
    int            nOutputs,
    int            nSamples
)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    int n, t, s, ch, i, band, Q, order, nSH;
    int o[MAX_SH_ORDER+2];
    const float_complex calpha = cmplxf(1.0f,0.0f), cbeta = cmplxf(0.0f, 0.0f);
    ARRAY2SH_CH_ORDER chOrdering;
    ARRAY2SH_NORM_TYPES norm;
    float gain_lin;
    
    /* reinit TFT if needed */
    array2sh_initTFT(hA2sh);
    
    /* compute encoding matrix if needed */
    if (pData->reinitSHTmatrixFLAG) {
        array2sh_calculate_sht_matrix(hA2sh); /* compute encoding matrix */
        array2sh_calculate_mag_curves(hA2sh); /* calculate magnitude response curves */

        /* reset FIFO buffers */
        pData->FIFO_idx = 0;
        memset(pData->inFIFO, 0, MAX_NUM_SENSORS*FRAME_SIZE*sizeof(float));
        memset(pData->outFIFO, 0, MAX_NUM_SH_SIGNALS*FRAME_SIZE*sizeof(float));

        pData->reinitSHTmatrixFLAG = 0;
    }

    /* local copy of user parameters */
    chOrdering = pData->chOrdering;
    norm = pData->norm;
    gain_lin = powf(10.0f, pData->gain_dB/20.0f);
    Q = arraySpecs->Q;
    order = pData->order;
    nSH = (order+1)*(order+1);

    /* Loop over all samples */
    for(s=0; s<nSamples; s++){
        /* Load input signals into inFIFO buffer */
        for(ch=0; ch<MIN(nInputs,Q); ch++)
            pData->inFIFO[ch][pData->FIFO_idx] = inputs[ch][s];
        for(; ch<Q; ch++) /* Zero any channels that were not given */
            pData->inFIFO[ch][pData->FIFO_idx] = 0.0f;

        /* Pull output signals from outFIFO buffer */
        for(ch=0; ch<MIN(nOutputs, MAX_NUM_SH_SIGNALS); ch++)
            outputs[ch][s] = pData->outFIFO[ch][pData->FIFO_idx];
        for(; ch<nOutputs; ch++) /* Zero any extra channels */
            outputs[ch][s] = 0.0f;

        /* Increment buffer index */
        pData->FIFO_idx++;

        /* Process frame if inFIFO is full and codec is ready for it */
        if (pData->FIFO_idx >= FRAME_SIZE && (pData->reinitSHTmatrixFLAG==0) ) {
            pData->FIFO_idx = 0;
            pData->procStatus = PROC_STATUS_ONGOING;

            /* Load time-domain data */
            for(i=0; i < nInputs; i++)
                utility_svvcopy(pData->inFIFO[i], FRAME_SIZE, pData->inputFrameTD[i]);
            for(; i<Q; i++)
                memset(pData->inputFrameTD[i], 0, FRAME_SIZE * sizeof(float));

            /* Apply time-frequency transform (TFT) */
            for(t=0; t< TIME_SLOTS; t++) {
                for(ch = 0; ch < Q; ch++)
                    utility_svvcopy(&(pData->inputFrameTD[ch][t*HOP_SIZE]), HOP_SIZE, pData->tempHopFrameTD_in[ch]);
                afSTFTforward(pData->hSTFT, pData->tempHopFrameTD_in, pData->STFTInputFrameTF);
                for(band=0; band<HYBRID_BANDS; band++)
                    for(ch=0; ch < Q; ch++)
                        pData->inputframeTF[band][ch][t] = cmplxf(pData->STFTInputFrameTF[ch].re[band], pData->STFTInputFrameTF[ch].im[band]);
            }

            /* Apply spherical harmonic transform (SHT) */
            for(band=0; band<HYBRID_BANDS; band++){
                cblas_cgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, nSH, TIME_SLOTS, Q, &calpha,
                            pData->W[band], MAX_NUM_SENSORS,
                            pData->inputframeTF[band], TIME_SLOTS, &cbeta,
                            pData->SHframeTF[band], TIME_SLOTS);
            }

            /* inverse-TFT */
            for(t = 0; t < TIME_SLOTS; t++) {
                for(band = 0; band < HYBRID_BANDS; band++) {
                    for (ch = 0; ch < nSH; ch++) {
                        pData->STFTOutputFrameTF[ch].re[band] = gain_lin*crealf(pData->SHframeTF[band][ch][t]);
                        pData->STFTOutputFrameTF[ch].im[band] = gain_lin*cimagf(pData->SHframeTF[band][ch][t]);
                    }
                }
                afSTFTinverse(pData->hSTFT, pData->STFTOutputFrameTF, pData->tempHopFrameTD_out);

                /* copy SH signals to output buffer */
                switch(chOrdering){
                    case CH_ACN:  /* already ACN */
                        for (ch = 0; ch < MIN(nSH, nOutputs); ch++)
                            utility_svvcopy(pData->tempHopFrameTD_out[ch], HOP_SIZE, &(pData->outFIFO[ch][t* HOP_SIZE]));
                        for (; ch < nOutputs; ch++)
                            memset(&(outputs[ch][t* HOP_SIZE]), 0, HOP_SIZE*sizeof(float));
                        break;
                    case CH_FUMA: /* convert to FuMa, only for first-order */
                        if(nOutputs>=4){
                            utility_svvcopy(pData->tempHopFrameTD_out[0], HOP_SIZE, &(pData->outFIFO[0][t* HOP_SIZE]));
                            utility_svvcopy(pData->tempHopFrameTD_out[1], HOP_SIZE, &(pData->outFIFO[2][t* HOP_SIZE]));
                            utility_svvcopy(pData->tempHopFrameTD_out[2], HOP_SIZE, &(pData->outFIFO[3][t* HOP_SIZE]));
                            utility_svvcopy(pData->tempHopFrameTD_out[3], HOP_SIZE, &(pData->outFIFO[1][t* HOP_SIZE]));
                        }
                        break;
                }
            }

            /* apply normalisation scheme */
            for(n=0; n<MAX_SH_ORDER+2; n++){  o[n] = n*n;  }
            switch(norm){
                case NORM_N3D: /* already N3D */
                    break;
                case NORM_SN3D: /* convert to SN3D */
                    for (n = 0; n<order+1; n++)
                        for (ch = o[n]; ch < MIN(o[n+1],nOutputs); ch++)
                            for(i = 0; i<FRAME_SIZE; i++)
                                pData->outFIFO[ch][i] /= sqrtf(2.0f*(float)n+1.0f);
                    break;
                case NORM_FUMA: /* convert to FuMa, only for first-order */
                    if(nOutputs>=4){
                        for(i = 0; i<FRAME_SIZE; i++)
                            pData->outFIFO[0][i] /= sqrtf(2.0f);
                        for (ch = 1; ch<4; ch++)
                            for(i = 0; i<FRAME_SIZE; i++)
                                pData->outFIFO[ch][i] /= sqrtf(3.0f);
                    }
                    else
                        for(i=0; i<nOutputs; i++)
                            memset(pData->outFIFO[i], 0, FRAME_SIZE * sizeof(float));
                    break;
            }
        }
        else if(pData->FIFO_idx >= FRAME_SIZE){
            /* clear outFIFO if codec was not ready */
            pData->FIFO_idx = 0;
            memset(pData->outFIFO, 0, MAX_NUM_SH_SIGNALS*FRAME_SIZE*sizeof(float));
        }
    }

    pData->procStatus = PROC_STATUS_NOT_ONGOING;
}

/* Set Functions */

void array2sh_refreshSettings(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    pData->reinitSHTmatrixFLAG = 1;
    array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
}

void array2sh_setEncodingOrder(void* const hA2sh, int newOrder)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    
    if(pData->new_order != newOrder){
        pData->new_order = newOrder;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
    /* FUMA only supports 1st order */
    if(pData->new_order!=ENCODING_ORDER_FIRST && pData->chOrdering == CH_FUMA)
        pData->chOrdering = CH_ACN;
    if(pData->new_order!=ENCODING_ORDER_FIRST && pData->norm == NORM_FUMA)
        pData->norm = NORM_SN3D;
}

void array2sh_setRequestEncoderEvalFLAG(void* const hA2sh, int newState)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    pData->evalRequestedFLAG = newState;
}

void array2sh_setEvalStatus(void* const hA2sh, ARRAY2SH_EVAL_STATUS new_evalStatus)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    if(new_evalStatus==EVAL_STATUS_NOT_EVALUATED){
        /* Pause until current initialisation is complete */
        while(pData->evalStatus == EVAL_STATUS_EVALUATING)
            SAF_SLEEP(10);
    }
    pData->evalStatus = new_evalStatus;
}

void array2sh_setDiffEQpastAliasing(void* const hA2sh, int newState)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    if(pData->enableDiffEQpastAliasing != newState){
        pData->enableDiffEQpastAliasing = newState;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setPreset(void* const hA2sh, int preset)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    array2sh_initArray(arraySpecs,(ARRAY2SH_MICROPHONE_ARRAY_PRESETS)preset, &(pData->new_order), 0);
    pData->c = (ARRAY2SH_MICROPHONE_ARRAY_PRESETS)preset == MICROPHONE_ARRAY_PRESET_AALTO_HYDROPHONE ? 1484.0f : 343.0f;
    pData->reinitSHTmatrixFLAG = 1;
    array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
}

void array2sh_setSensorAzi_rad(void* const hA2sh, int index, float newAzi_rad)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    if(arraySpecs->sensorCoords_rad[index][0] != newAzi_rad){
        arraySpecs->sensorCoords_rad[index][0] = newAzi_rad;
        arraySpecs->sensorCoords_deg[index][0] = newAzi_rad * (180.0f/M_PI);
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setSensorElev_rad(void* const hA2sh, int index, float newElev_rad)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    if(arraySpecs->sensorCoords_rad[index][1] != newElev_rad){
        arraySpecs->sensorCoords_rad[index][1] = newElev_rad;
        arraySpecs->sensorCoords_deg[index][1] = newElev_rad * (180.0f/M_PI);
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setSensorAzi_deg(void* const hA2sh, int index, float newAzi_deg)

{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    if(arraySpecs->sensorCoords_deg[index][0] != newAzi_deg){
        arraySpecs->sensorCoords_rad[index][0] = newAzi_deg * (M_PI/180.0f);
        arraySpecs->sensorCoords_deg[index][0] = newAzi_deg;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setSensorElev_deg(void* const hA2sh, int index, float newElev_deg)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    if(arraySpecs->sensorCoords_deg[index][1] != newElev_deg){
        arraySpecs->sensorCoords_rad[index][1] = newElev_deg * (M_PI/180.0f);
        arraySpecs->sensorCoords_deg[index][1] = newElev_deg;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setNumSensors(void* const hA2sh, int newQ)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    int nSH;
    
    nSH = (pData->new_order+1)*(pData->new_order+1);
    if (newQ < nSH){
        pData->new_order = 1;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
    if(arraySpecs->Q != newQ){
        arraySpecs->newQ = newQ;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setr(void* const hA2sh, float newr)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    newr = CLAMP(newr, ARRAY2SH_ARRAY_RADIUS_MIN_VALUE/1e3f, ARRAY2SH_ARRAY_RADIUS_MAX_VALUE/1e3f);
    if(arraySpecs->r!=newr){
        arraySpecs->r = newr;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setR(void* const hA2sh, float newR)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    newR = CLAMP(newR, ARRAY2SH_BAFFLE_RADIUS_MIN_VALUE/1e3f, ARRAY2SH_BAFFLE_RADIUS_MAX_VALUE/1e3f);
    if(arraySpecs->R!=newR){
        arraySpecs->R = newR;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setArrayType(void* const hA2sh, int newType)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    if(arraySpecs->arrayType != (ARRAY2SH_ARRAY_TYPES)newType){
        arraySpecs->arrayType = (ARRAY2SH_ARRAY_TYPES)newType;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setWeightType(void* const hA2sh, int newType)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    
    if(arraySpecs->weightType!=(ARRAY2SH_WEIGHT_TYPES)newType){
        arraySpecs->weightType = (ARRAY2SH_WEIGHT_TYPES)newType;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setFilterType(void* const hA2sh, int newType)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    
    if(pData->filterType!=(ARRAY2SH_FILTER_TYPES)newType){
        pData->filterType = (ARRAY2SH_FILTER_TYPES)newType;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setRegPar(void* const hA2sh, float newVal)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
     newVal = CLAMP(newVal, ARRAY2SH_MAX_GAIN_MIN_VALUE, ARRAY2SH_MAX_GAIN_MAX_VALUE);
    if(pData->regPar!=newVal){
        pData->regPar = newVal;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setChOrder(void* const hA2sh, int newOrder)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    if((ARRAY2SH_CH_ORDER)newOrder != CH_FUMA || pData->order==ENCODING_ORDER_FIRST)/* FUMA only supports 1st order */
        pData->chOrdering = (ARRAY2SH_CH_ORDER)newOrder;
}

void array2sh_setNormType(void* const hA2sh, int newType)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    if((ARRAY2SH_NORM_TYPES)newType != NORM_FUMA || pData->order==ENCODING_ORDER_FIRST)/* FUMA only supports 1st order */
        pData->norm = (ARRAY2SH_NORM_TYPES)newType;
}

void array2sh_setc(void* const hA2sh, float newc)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    newc = CLAMP(newc, ARRAY2SH_SPEED_OF_SOUND_MIN_VALUE, ARRAY2SH_SPEED_OF_SOUND_MAX_VALUE);
    if(newc!=pData->c){
        pData->c = newc;
        pData->reinitSHTmatrixFLAG = 1;
        array2sh_setEvalStatus(hA2sh, EVAL_STATUS_NOT_EVALUATED);
    }
}

void array2sh_setGain(void* const hA2sh, float newGain)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    pData->gain_dB = CLAMP(newGain, ARRAY2SH_POST_GAIN_MIN_VALUE, ARRAY2SH_POST_GAIN_MAX_VALUE);
}


/* Get Functions */

ARRAY2SH_EVAL_STATUS array2sh_getEvalStatus(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->evalStatus;
}

float array2sh_getProgressBar0_1(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->progressBar0_1;
}

void array2sh_getProgressBarText(void* const hA2sh, char* text)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    memcpy(text, pData->progressBarText, ARRAY2SH_PROGRESSBARTEXT_CHAR_LENGTH*sizeof(char));
}

int array2sh_getRequestEncoderEvalFLAG(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->evalRequestedFLAG;
}

int array2sh_getDiffEQpastAliasing(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->enableDiffEQpastAliasing;
}

int array2sh_getEncodingOrder(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->new_order;
}

float array2sh_getSensorAzi_rad(void* const hA2sh, int index)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return arraySpecs->sensorCoords_rad[index][0];
}

float array2sh_getSensorElev_rad(void* const hA2sh, int index)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return arraySpecs->sensorCoords_rad[index][1];
}

float array2sh_getSensorAzi_deg(void* const hA2sh, int index)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return arraySpecs->sensorCoords_deg[index][0];
}

float array2sh_getSensorElev_deg(void* const hA2sh, int index)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return arraySpecs->sensorCoords_deg[index][1];
}

int array2sh_getNumSensors(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
   // return arraySpecs->Q;
    return arraySpecs->newQ; /* return the new Q, incase the plug-in is still waiting for a refresh */
}

int array2sh_getMaxNumSensors(void)
{
    return MAX_NUM_SENSORS;
}

int array2sh_getMinNumSensors(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return (pData->new_order+1)*(pData->new_order+1);
}

int array2sh_getNSHrequired(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return (pData->new_order+1)*(pData->new_order+1);
}

float array2sh_getr(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return arraySpecs->r;
}

float array2sh_getR(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return arraySpecs->R;
} 

int array2sh_getArrayType(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return (int)arraySpecs->arrayType;
}

int array2sh_getWeightType(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    array2sh_arrayPars* arraySpecs = (array2sh_arrayPars*)(pData->arraySpecs);
    return (int)arraySpecs->weightType;
}

int array2sh_getFilterType(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return (int)pData->filterType;
}

float array2sh_getRegPar(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->regPar;
}

int array2sh_getChOrder(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return (int)pData->chOrdering;
}

int array2sh_getNormType(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return (int)pData->norm;
}

float array2sh_getc(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->c;
}

float array2sh_getGain(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->gain_dB;
}

float* array2sh_getFreqVector(void* const hA2sh, int* nFreqPoints)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    (*nFreqPoints) = HYBRID_BANDS;
    return &(pData->freqVector[0]);
}

float** array2sh_getbN_inv(void* const hA2sh, int* nCurves, int* nFreqPoints)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    (*nCurves) = pData->order+1;
    (*nFreqPoints) = HYBRID_BANDS;
    return pData->bN_inv_dB;
}

float** array2sh_getbN_modal(void* const hA2sh, int* nCurves, int* nFreqPoints)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    (*nCurves) = pData->order+1;
    (*nFreqPoints) = HYBRID_BANDS;
    return pData->bN_modal_dB;
}

float* array2sh_getSpatialCorrelation_Handle(void* const hA2sh, int* nCurves, int* nFreqPoints)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    (*nCurves) = pData->order+1;
    (*nFreqPoints) = HYBRID_BANDS;
    return pData->cSH;
}

float* array2sh_getLevelDifference_Handle(void* const hA2sh, int* nCurves, int* nFreqPoints)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    (*nCurves) = pData->order+1;
    (*nFreqPoints) = HYBRID_BANDS;
    return pData->lSH;
}

int array2sh_getSamplingRate(void* const hA2sh)
{
    array2sh_data *pData = (array2sh_data*)(hA2sh);
    return pData->fs;
}

int array2sh_getProcessingDelay()
{
    return FRAME_SIZE + 12*HOP_SIZE;
}
