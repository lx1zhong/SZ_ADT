/**
 *  @file szd_float_pwr.c
 *  @author Sheng Di, Dingwen Tao, Xin Liang, Xiangyu Zou, Tao Lu, Wen Xia, Xuan Wang, Weizhe Zhang
 *  @date Feb., 2019
 *  @brief 
 *  (C) 2016 by Mathematics and Computer Science (MCS), Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include <stdlib.h> 
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "TightDataPointStorageF.h"
#include "CompressElement.h"
#include "sz.h"
#include "Huffman.h"
#include "sz_float_pwr.h"
#include "utility.h"
#include "transcode.h"
//#include "rw.h"
//
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wchar-subscripts"

#define TIMER__
#ifdef TIMER__
#include <sys/time.h>

struct timeval Start7; /*only used for recording the cost*/
double huffCost7 = 0;

void huff_cost_start7()
{
	huffCost7 = 0;
	gettimeofday(&Start7, NULL);
}

void huff_cost_end7()
{
	double elapsed;
	struct timeval costEnd;
	gettimeofday(&costEnd, NULL);
	elapsed = ((costEnd.tv_sec*1000000+costEnd.tv_usec)-(Start7.tv_sec*1000000+Start7.tv_usec))/1000000.0;
	huffCost7 += elapsed;
}
#endif

void decompressDataSeries_float_1D_pwr(float** data, size_t dataSeriesLength, TightDataPointStorageF* tdps) 
{
	updateQuantizationInfo(tdps->intervals);
	unsigned char tmpPrecBytes[4] = {0}; //used when needing to convert bytes to float values
	unsigned char* bp = tdps->pwrErrBoundBytes;
	size_t i, j, k = 0, p = 0, l = 0; // k is to track the location of residual_bit
								// in resiMidBits, p is to track the
								// byte_index of resiMidBits, l is for
								// leadNum
	unsigned char* leadNum;
	float interval = 0;// = (float)tdps->realPrecision*2;
	
	convertByteArray2IntArray_fast_2b(tdps->exactDataNum, tdps->leadNumArray, tdps->leadNumArray_size, &leadNum);

	*data = (float*)malloc(sizeof(float)*dataSeriesLength);

	int* type = (int*)malloc(dataSeriesLength*sizeof(int));

#ifdef TIMER__
	huff_cost_start7();
#endif
	if (tdps->entropyType == 0) {
		HuffmanTree* huffmanTree = createHuffmanTree(tdps->stateNum);
		decode_withTree(huffmanTree, tdps->typeArray, dataSeriesLength, type);
		SZ_ReleaseHuffman(huffmanTree);	
	}
	else if (tdps->entropyType == 1) {
		short* type_ = (short*)malloc(dataSeriesLength*sizeof(short));
		int tmpSize = sz_lossless_decompress(confparams_cpr->losslessCompressor, tdps->typeArray, tdps->typeArray_size, (unsigned char **)&type_, dataSeriesLength*sizeof(short));
		if (tmpSize != dataSeriesLength*sizeof(short)){
			printf("tmpSize(%d) != dataSeriesLength*sizeof(short)(%lu)",tmpSize,dataSeriesLength*sizeof(short));
		}
		for (int i=0; i<dataSeriesLength; i++)
			type[i] = (int)type_[i];
		free(type);
	}
	else {
		decode_with_fse(type, dataSeriesLength, tdps->intervals, tdps->FseCode, tdps->FseCode_size, 
					tdps->transCodeBits, tdps->transCodeBits_size);
	}
#ifdef TIMER__
    huff_cost_end7();
    printf("[decoder]: time=%f\n", huffCost7);

#endif

	//sdi:Debug
	//writeUShortData(type, dataSeriesLength, "decompressStateBytes.sb");

	unsigned char preBytes[4];
	unsigned char curBytes[4];
	
	memset(preBytes, 0, 4);

	size_t curByteIndex = 0;
	int reqLength = 0, reqBytesLength = 0, resiBitsLength = 0, resiBits = 0; 
	unsigned char leadingNum;	
	float medianValue, exactData, predValue = 0, realPrecision = 0;
	
	medianValue = tdps->medianValue;
	
	int type_, updateReqLength = 0;
	for (i = 0; i < dataSeriesLength; i++) 
	{
		if(i%tdps->segment_size==0)
		{
			tmpPrecBytes[0] = *(bp++);
			tmpPrecBytes[1] = *(bp++);
			tmpPrecBytes[2] = 0;
			tmpPrecBytes[3] = 0;
			realPrecision = bytesToFloat(tmpPrecBytes);
			interval = realPrecision*2;
			updateReqLength = 0;
		}
		
		type_ = type[i];
		switch (type_) {
		case 0:
			// compute resiBits
			if(updateReqLength==0)
			{
				computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
				reqBytesLength = reqLength/8;
				resiBitsLength = reqLength%8;	
				updateReqLength = 1;	
			}
			resiBits = 0;
			if (resiBitsLength != 0) {
				int kMod8 = k % 8;
				int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
				if (rightMovSteps > 0) {
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
				} else if (rightMovSteps < 0) {
					int code1 = getLeftMovingCode(kMod8);
					int code2 = getRightMovingCode(kMod8, resiBitsLength);
					int leftMovSteps = -rightMovSteps;
					rightMovSteps = 8 - leftMovSteps;
					resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
					p++;
					resiBits = resiBits
							| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
				} else // rightMovSteps == 0
				{
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code);
					p++;
				}
				k += resiBitsLength;
			}

			// recover the exact data	
			memset(curBytes, 0, 4);
			leadingNum = leadNum[l++];
			memcpy(curBytes, preBytes, leadingNum);
			for (j = leadingNum; j < reqBytesLength; j++)
				curBytes[j] = tdps->exactMidBytes[curByteIndex++];
			if (resiBitsLength != 0) {
				unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
				curBytes[reqBytesLength] = resiByte;
			}
			
			exactData = bytesToFloat(curBytes);
			(*data)[i] = exactData + medianValue;
			memcpy(preBytes,curBytes,4);
			break;
		default:
			//predValue = 2 * (*data)[i-1] - (*data)[i-2];
			predValue = (*data)[i-1];
			(*data)[i] = predValue + (type_-exe_params->intvRadius)*interval;
			break;
		}
		//printf("%.30G\n",(*data)[i]);
	}
	free(leadNum);
	free(type);
	return;
}

float* extractRealPrecision_2D_float(size_t R1, size_t R2, int blockSize, TightDataPointStorageF* tdps)
{
	size_t i,j,k=0, I;
	unsigned char* bytes = tdps->pwrErrBoundBytes;
	unsigned char tmpBytes[4] = {0};
	float* result = (float*)malloc(sizeof(float)*R1*R2);
	for(i=0;i<R1;i++)
	{
		I = i*R2;
		for(j=0;j<R2;j++)
		{
			tmpBytes[0] = bytes[k++];
			tmpBytes[1] = bytes[k++];
			result[I+j]=bytesToFloat(tmpBytes);
		}
	}
	return result;
}

void decompressDataSeries_float_2D_pwr(float** data, size_t r1, size_t r2, TightDataPointStorageF* tdps) 
{
	updateQuantizationInfo(tdps->intervals);
	//printf("tdps->intervals=%d, exe_params->intvRadius=%d\n", tdps->intervals, exe_params->intvRadius);
	
	size_t j, k = 0, p = 0, l = 0; // k is to track the location of residual_bit
	// in resiMidBits, p is to track the
	// byte_index of resiMidBits, l is for
	// leadNum
	size_t dataSeriesLength = r1*r2;
	//	printf ("%d %d\n", r1, r2);

	unsigned char* leadNum;

	convertByteArray2IntArray_fast_2b(tdps->exactDataNum, tdps->leadNumArray, tdps->leadNumArray_size, &leadNum);

	*data = (float*)malloc(sizeof(float)*dataSeriesLength);

	int* type = (int*)malloc(dataSeriesLength*sizeof(int));

#ifdef TIMER__
	huff_cost_start7();
#endif
	if (tdps->entropyType == 0) {
		HuffmanTree* huffmanTree = createHuffmanTree(tdps->stateNum);
		decode_withTree(huffmanTree, tdps->typeArray, dataSeriesLength, type);
		SZ_ReleaseHuffman(huffmanTree);	
	}
	else if (tdps->entropyType == 1) {
		short* type_ = (short*)malloc(dataSeriesLength*sizeof(short));
		int tmpSize = sz_lossless_decompress(confparams_cpr->losslessCompressor, tdps->typeArray, tdps->typeArray_size, (unsigned char **)&type_, dataSeriesLength*sizeof(short));
		if (tmpSize != dataSeriesLength*sizeof(short)){
			printf("tmpSize(%d) != dataSeriesLength*sizeof(short)(%lu)",tmpSize,dataSeriesLength*sizeof(short));
		}
		for (int i=0; i<dataSeriesLength; i++)
			type[i] = (int)type_[i];
		free(type);
	}
	else {
		decode_with_fse(type, dataSeriesLength, tdps->intervals, tdps->FseCode, tdps->FseCode_size, 
					tdps->transCodeBits, tdps->transCodeBits_size);
	}
#ifdef TIMER__
    huff_cost_end7();
    printf("[decoder]: time=%f\n", huffCost7);

#endif

	unsigned char preBytes[4];
	unsigned char curBytes[4];

	memset(preBytes, 0, 4);

	size_t curByteIndex = 0;
	int reqLength, reqBytesLength, resiBitsLength, resiBits; 
	unsigned char leadingNum;	
	float medianValue, exactData, realPrecision;
	int type_;	
	float pred1D, pred2D;
	size_t ii, jj, II = 0, JJ = 0, updateReqLength = 1;

	int blockSize = computeBlockEdgeSize_2D(tdps->segment_size);
	size_t R1 = 1+(r1-1)/blockSize;
	size_t R2 = 1+(r2-1)/blockSize;		
	float* pwrErrBound = extractRealPrecision_2D_float(R1, R2, blockSize, tdps);

	realPrecision = pwrErrBound[0];	
	computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
	reqBytesLength = reqLength/8;
	resiBitsLength = reqLength%8;

	/* Process Row-0, data 0 */

	// compute resiBits
	resiBits = 0;
	if (resiBitsLength != 0) {
		int kMod8 = k % 8;
		int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
		if (rightMovSteps > 0) {
			int code = getRightMovingCode(kMod8, resiBitsLength);
			resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
		} else if (rightMovSteps < 0) {
			int code1 = getLeftMovingCode(kMod8);
			int code2 = getRightMovingCode(kMod8, resiBitsLength);
			int leftMovSteps = -rightMovSteps;
			rightMovSteps = 8 - leftMovSteps;
			resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
			p++;
			resiBits = resiBits
					| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
		} else // rightMovSteps == 0
		{
			int code = getRightMovingCode(kMod8, resiBitsLength);
			resiBits = (tdps->residualMidBits[p] & code);
			p++;
		}
		k += resiBitsLength;
	}

	// recover the exact data
	memset(curBytes, 0, 4);
	leadingNum = leadNum[l++];
	memcpy(curBytes, preBytes, leadingNum);
	for (j = leadingNum; j < reqBytesLength; j++)
		curBytes[j] = tdps->exactMidBytes[curByteIndex++];
	if (resiBitsLength != 0) {
		unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
		curBytes[reqBytesLength] = resiByte;
	}

	exactData = bytesToFloat(curBytes);
	(*data)[0] = exactData + medianValue;
	memcpy(preBytes,curBytes,4);

	/* Process Row-0, data 1 */
	type_ = type[1];
	if (type_ != 0)
	{
		pred1D = (*data)[0];
		(*data)[1] = pred1D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
	}
	else
	{
		// compute resiBits		
		resiBits = 0;
		if (resiBitsLength != 0) {
			int kMod8 = k % 8;
			int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
			if (rightMovSteps > 0) {
				int code = getRightMovingCode(kMod8, resiBitsLength);
				resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
			} else if (rightMovSteps < 0) {
				int code1 = getLeftMovingCode(kMod8);
				int code2 = getRightMovingCode(kMod8, resiBitsLength);
				int leftMovSteps = -rightMovSteps;
				rightMovSteps = 8 - leftMovSteps;
				resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
				p++;
				resiBits = resiBits
						| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
			} else // rightMovSteps == 0
			{
				int code = getRightMovingCode(kMod8, resiBitsLength);
				resiBits = (tdps->residualMidBits[p] & code);
				p++;
			}
			k += resiBitsLength;
		}

		// recover the exact data
		memset(curBytes, 0, 4);
		leadingNum = leadNum[l++];
		memcpy(curBytes, preBytes, leadingNum);
		for (j = leadingNum; j < reqBytesLength; j++)
			curBytes[j] = tdps->exactMidBytes[curByteIndex++];
		if (resiBitsLength != 0) {
			unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
			curBytes[reqBytesLength] = resiByte;
		}

		exactData = bytesToFloat(curBytes);
		(*data)[1] = exactData + medianValue;
		memcpy(preBytes,curBytes,4);
	}

	/* Process Row-0, data 2 --> data r2-1 */
	for (jj = 2; jj < r2; jj++)
	{	
		if(jj%blockSize==0)
		{
			II = 0;
			JJ++;
			realPrecision = pwrErrBound[JJ];
			updateReqLength = 0;			
		}

		type_ = type[jj];
		if (type_ != 0)
		{
			pred1D = 2*(*data)[jj-1] - (*data)[jj-2];						
			(*data)[jj] = pred1D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
		}
		else
		{
			// compute resiBits
			if(updateReqLength==0)
			{
				computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
				reqBytesLength = reqLength/8;
				resiBitsLength = reqLength%8;				
				updateReqLength = 1;
			}
			
			resiBits = 0;
			if (resiBitsLength != 0) {
				int kMod8 = k % 8;
				int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
				if (rightMovSteps > 0) {
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
				} else if (rightMovSteps < 0) {
					int code1 = getLeftMovingCode(kMod8);
					int code2 = getRightMovingCode(kMod8, resiBitsLength);
					int leftMovSteps = -rightMovSteps;
					rightMovSteps = 8 - leftMovSteps;
					resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
					p++;
					resiBits = resiBits
							| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
				} else // rightMovSteps == 0
				{
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code);
					p++;
				}
				k += resiBitsLength;
			}

			// recover the exact data
			memset(curBytes, 0, 4);
			leadingNum = leadNum[l++];
			memcpy(curBytes, preBytes, leadingNum);
			for (j = leadingNum; j < reqBytesLength; j++)
				curBytes[j] = tdps->exactMidBytes[curByteIndex++];
			if (resiBitsLength != 0) {
				unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
				curBytes[reqBytesLength] = resiByte;
			}

			exactData = bytesToFloat(curBytes);
			(*data)[jj] = exactData + medianValue;
			memcpy(preBytes,curBytes,4);
		}
	}

	size_t index;
	/* Process Row-1 --> Row-r1-1 */
	for (ii = 1; ii < r1; ii++)
	{
		/* Process row-ii data 0 */		
		if(ii%blockSize==0)
			II++;
		JJ = 0;
		realPrecision = pwrErrBound[II*R2+JJ];				
		updateReqLength = 0;
		
		index = ii*r2;

		type_ = type[index];
		if (type_ != 0)
		{
			pred1D = (*data)[index-r2];		
			(*data)[index] = pred1D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
		}
		else
		{
			// compute resiBits
			if(updateReqLength==0)
			{
				computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
				reqBytesLength = reqLength/8;
				resiBitsLength = reqLength%8;				
				updateReqLength = 1;
			}
			
			resiBits = 0;
			if (resiBitsLength != 0) {
				int kMod8 = k % 8;
				int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
				if (rightMovSteps > 0) {
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
				} else if (rightMovSteps < 0) {
					int code1 = getLeftMovingCode(kMod8);
					int code2 = getRightMovingCode(kMod8, resiBitsLength);
					int leftMovSteps = -rightMovSteps;
					rightMovSteps = 8 - leftMovSteps;
					resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
					p++;
					resiBits = resiBits
							| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
				} else // rightMovSteps == 0
				{
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code);
					p++;
				}
				k += resiBitsLength;
			}

			// recover the exact data
			memset(curBytes, 0, 4);
			leadingNum = leadNum[l++];
			memcpy(curBytes, preBytes, leadingNum);
			for (j = leadingNum; j < reqBytesLength; j++)
				curBytes[j] = tdps->exactMidBytes[curByteIndex++];
			if (resiBitsLength != 0) {
				unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
				curBytes[reqBytesLength] = resiByte;
			}

			exactData = bytesToFloat(curBytes);
			(*data)[index] = exactData + medianValue;
			memcpy(preBytes,curBytes,4);
		}

		/* Process row-ii data 1 --> r2-1*/
		for (jj = 1; jj < r2; jj++)
		{
			index = ii*r2+jj;
			
			if(jj%blockSize==0)
				JJ++;
			realPrecision = pwrErrBound[II*R2+JJ];			
			updateReqLength = 0;			

			type_ = type[index];
			if (type_ != 0)
			{
				pred2D = (*data)[index-1] + (*data)[index-r2] - (*data)[index-r2-1];
				(*data)[index] = pred2D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
			}
			else
			{
				// compute resiBits
				if(updateReqLength==0)
				{
					computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
					reqBytesLength = reqLength/8;
					resiBitsLength = reqLength%8;				
					updateReqLength = 1;
				}				
				
				resiBits = 0;
				if (resiBitsLength != 0) {
					int kMod8 = k % 8;
					int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
					if (rightMovSteps > 0) {
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
					} else if (rightMovSteps < 0) {
						int code1 = getLeftMovingCode(kMod8);
						int code2 = getRightMovingCode(kMod8, resiBitsLength);
						int leftMovSteps = -rightMovSteps;
						rightMovSteps = 8 - leftMovSteps;
						resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
						p++;
						resiBits = resiBits
								| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
					} else // rightMovSteps == 0
					{
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code);
						p++;
					}
					k += resiBitsLength;
				}

				// recover the exact data
				memset(curBytes, 0, 4);
				leadingNum = leadNum[l++];
				memcpy(curBytes, preBytes, leadingNum);
				for (j = leadingNum; j < reqBytesLength; j++)
					curBytes[j] = tdps->exactMidBytes[curByteIndex++];
				if (resiBitsLength != 0) {
					unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
					curBytes[reqBytesLength] = resiByte;
				}

				exactData = bytesToFloat(curBytes);
				(*data)[index] = exactData + medianValue;
				memcpy(preBytes,curBytes,4);
			}
		}
	}

	free(pwrErrBound);
	free(leadNum);
	free(type);
	return;
}

float* extractRealPrecision_3D_float(size_t R1, size_t R2, size_t R3, int blockSize, TightDataPointStorageF* tdps)
{
	size_t i,j,k=0, IR, JR, p = 0;
	size_t R23 = R2*R3;
	unsigned char* bytes = tdps->pwrErrBoundBytes;
	unsigned char tmpBytes[4] = {0};
	float* result = (float*)malloc(sizeof(float)*R1*R2*R3);
	for(i=0;i<R1;i++)
	{
		IR = i*R23;
		for(j=0;j<R2;j++)
		{
			JR = j*R3;
			for(k=0;k<R3;k++)
			{
				tmpBytes[0] = bytes[p++];
				tmpBytes[1] = bytes[p++];
				result[IR+JR+k]=bytesToFloat(tmpBytes);				
			}
		}
	}
	return result;
}

void decompressDataSeries_float_3D_pwr(float** data, size_t r1, size_t r2, size_t r3, TightDataPointStorageF* tdps) 
{
	updateQuantizationInfo(tdps->intervals);
	size_t j, k = 0, p = 0, l = 0; // k is to track the location of residual_bit
	// in resiMidBits, p is to track the
	// byte_index of resiMidBits, l is for
	// leadNum
	size_t dataSeriesLength = r1*r2*r3;
	size_t r23 = r2*r3;
//	printf ("%d %d %d\n", r1, r2, r3);
	unsigned char* leadNum;

	convertByteArray2IntArray_fast_2b(tdps->exactDataNum, tdps->leadNumArray, tdps->leadNumArray_size, &leadNum);

	*data = (float*)malloc(sizeof(float)*dataSeriesLength);
	int* type = (int*)malloc(dataSeriesLength*sizeof(int));

#ifdef TIMER__
	huff_cost_start7();
#endif
	if (tdps->entropyType == 0) {
		HuffmanTree* huffmanTree = createHuffmanTree(tdps->stateNum);
		decode_withTree(huffmanTree, tdps->typeArray, dataSeriesLength, type);
		SZ_ReleaseHuffman(huffmanTree);	
	}
	else if (tdps->entropyType == 1) {
		short* type_ = (short*)malloc(dataSeriesLength*sizeof(short));
		int tmpSize = sz_lossless_decompress(confparams_cpr->losslessCompressor, tdps->typeArray, tdps->typeArray_size, (unsigned char **)&type_, dataSeriesLength*sizeof(short));
		if (tmpSize != dataSeriesLength*sizeof(short)){
			printf("tmpSize(%d) != dataSeriesLength*sizeof(short)(%lu)",tmpSize,dataSeriesLength*sizeof(short));
		}
		for (int i=0; i<dataSeriesLength; i++)
			type[i] = (int)type_[i];
		free(type);
	}
	else {
		decode_with_fse(type, dataSeriesLength, tdps->intervals, tdps->FseCode, tdps->FseCode_size, 
					tdps->transCodeBits, tdps->transCodeBits_size);
	}
#ifdef TIMER__
    huff_cost_end7();
    printf("[decoder]: time=%f\n", huffCost7);

#endif

	unsigned char preBytes[4];
	unsigned char curBytes[4];

	memset(preBytes, 0, 4);
	size_t curByteIndex = 0;
	int reqLength, reqBytesLength, resiBitsLength, resiBits; 
	unsigned char leadingNum;
	float medianValue, exactData, realPrecision;
	int type_;
	float pred1D, pred2D, pred3D;
	size_t ii, jj, kk, II = 0, JJ = 0, KK = 0, updateReqLength = 1;

	int blockSize = computeBlockEdgeSize_3D(tdps->segment_size);
	size_t R1 = 1+(r1-1)/blockSize;
	size_t R2 = 1+(r2-1)/blockSize;		
	size_t R3 = 1+(r3-1)/blockSize;
	size_t R23 = R2*R3;
	float* pwrErrBound = extractRealPrecision_3D_float(R1, R2, R3, blockSize, tdps);

	realPrecision = pwrErrBound[0];	
	computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
	reqBytesLength = reqLength/8;
	resiBitsLength = reqLength%8;

	///////////////////////////	Process layer-0 ///////////////////////////
	/* Process Row-0 data 0*/
	// compute resiBits
	resiBits = 0;
	if (resiBitsLength != 0) {
		int kMod8 = k % 8;
		int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
		if (rightMovSteps > 0) {
			int code = getRightMovingCode(kMod8, resiBitsLength);
			resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
		} else if (rightMovSteps < 0) {
			int code1 = getLeftMovingCode(kMod8);
			int code2 = getRightMovingCode(kMod8, resiBitsLength);
			int leftMovSteps = -rightMovSteps;
			rightMovSteps = 8 - leftMovSteps;
			resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
			p++;
			resiBits = resiBits
					| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
		} else // rightMovSteps == 0
		{
			int code = getRightMovingCode(kMod8, resiBitsLength);
			resiBits = (tdps->residualMidBits[p] & code);
			p++;
		}
		k += resiBitsLength;
	}

	// recover the exact data
	memset(curBytes, 0, 4);
	leadingNum = leadNum[l++];
	memcpy(curBytes, preBytes, leadingNum);
	for (j = leadingNum; j < reqBytesLength; j++)
		curBytes[j] = tdps->exactMidBytes[curByteIndex++];
	if (resiBitsLength != 0) {
		unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
		curBytes[reqBytesLength] = resiByte;
	}
	exactData = bytesToFloat(curBytes);
	(*data)[0] = exactData + medianValue;
	memcpy(preBytes,curBytes,4);

	/* Process Row-0, data 1 */
	pred1D = (*data)[0];

	type_ = type[1];
	if (type_ != 0)
	{
		(*data)[1] = pred1D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
	}
	else
	{
		// compute resiBits
		resiBits = 0;
		if (resiBitsLength != 0) {
			int kMod8 = k % 8;
			int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
			if (rightMovSteps > 0) {
				int code = getRightMovingCode(kMod8, resiBitsLength);
				resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
			} else if (rightMovSteps < 0) {
				int code1 = getLeftMovingCode(kMod8);
				int code2 = getRightMovingCode(kMod8, resiBitsLength);
				int leftMovSteps = -rightMovSteps;
				rightMovSteps = 8 - leftMovSteps;
				resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
				p++;
				resiBits = resiBits
						| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
			} else // rightMovSteps == 0
			{
				int code = getRightMovingCode(kMod8, resiBitsLength);
				resiBits = (tdps->residualMidBits[p] & code);
				p++;
			}
			k += resiBitsLength;
		}

		// recover the exact data
		memset(curBytes, 0, 4);
		leadingNum = leadNum[l++];
		memcpy(curBytes, preBytes, leadingNum);
		for (j = leadingNum; j < reqBytesLength; j++)
			curBytes[j] = tdps->exactMidBytes[curByteIndex++];
		if (resiBitsLength != 0) {
			unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
			curBytes[reqBytesLength] = resiByte;
		}

		exactData = bytesToFloat(curBytes);
		(*data)[1] = exactData + medianValue;
		memcpy(preBytes,curBytes,4);
	}
	/* Process Row-0, data 2 --> data r3-1 */
	for (jj = 2; jj < r3; jj++)
	{
		if(jj%blockSize==0)
		{
			KK = 0;//dimension 1 (top)
			II = 0;//dimension 2 (mid)
			JJ++;
			realPrecision = pwrErrBound[JJ];
			updateReqLength = 0;			
		}		
		type_ = type[jj];
		if (type_ != 0)
		{
			pred1D = 2*(*data)[jj-1] - (*data)[jj-2];
			(*data)[jj] = pred1D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
		}
		else
		{
			// compute resiBits
			if(updateReqLength==0)
			{
				computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
				reqBytesLength = reqLength/8;
				resiBitsLength = reqLength%8;				
				updateReqLength = 1;
			}

			resiBits = 0;
			if (resiBitsLength != 0) {
				int kMod8 = k % 8;
				int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
				if (rightMovSteps > 0) {
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
				} else if (rightMovSteps < 0) {
					int code1 = getLeftMovingCode(kMod8);
					int code2 = getRightMovingCode(kMod8, resiBitsLength);
					int leftMovSteps = -rightMovSteps;
					rightMovSteps = 8 - leftMovSteps;
					resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
					p++;
					resiBits = resiBits
							| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
				} else // rightMovSteps == 0
				{
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code);
					p++;
				}
				k += resiBitsLength;
			}

			// recover the exact data
			memset(curBytes, 0, 4);
			leadingNum = leadNum[l++];
			memcpy(curBytes, preBytes, leadingNum);
			for (j = leadingNum; j < reqBytesLength; j++)
				curBytes[j] = tdps->exactMidBytes[curByteIndex++];
			if (resiBitsLength != 0) {
				unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
				curBytes[reqBytesLength] = resiByte;
			}

			exactData = bytesToFloat(curBytes);
			(*data)[jj] = exactData + medianValue;
			memcpy(preBytes,curBytes,4);
		}
	}
	size_t index;
	/* Process Row-1 --> Row-r2-1 */
	for (ii = 1; ii < r2; ii++)
	{
		/* Process row-ii data 0 */		
		if(ii%blockSize==0)
			II++;		
		JJ = 0;
		realPrecision = pwrErrBound[II*R3+JJ];
		updateReqLength = 0;		

		index = ii*r3;
		
		type_ = type[index];
		if (type_ != 0)
		{
			pred1D = (*data)[index-r3];			
			(*data)[index] = pred1D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
		}
		else
		{
			// compute resiBits
			if(updateReqLength==0)
			{
				computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
				reqBytesLength = reqLength/8;
				resiBitsLength = reqLength%8;				
				updateReqLength = 1;
			}
			
			resiBits = 0;
			if (resiBitsLength != 0) {
				int kMod8 = k % 8;
				int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
				if (rightMovSteps > 0) {
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
				} else if (rightMovSteps < 0) {
					int code1 = getLeftMovingCode(kMod8);
					int code2 = getRightMovingCode(kMod8, resiBitsLength);
					int leftMovSteps = -rightMovSteps;
					rightMovSteps = 8 - leftMovSteps;
					resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
					p++;
					resiBits = resiBits
							| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
				} else // rightMovSteps == 0
				{
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code);
					p++;
				}
				k += resiBitsLength;
			}

			// recover the exact data
			memset(curBytes, 0, 4);
			leadingNum = leadNum[l++];
			memcpy(curBytes, preBytes, leadingNum);
			for (j = leadingNum; j < reqBytesLength; j++)
				curBytes[j] = tdps->exactMidBytes[curByteIndex++];
			if (resiBitsLength != 0) {
				unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
				curBytes[reqBytesLength] = resiByte;
			}

			exactData = bytesToFloat(curBytes);
			(*data)[index] = exactData + medianValue;
			memcpy(preBytes,curBytes,4);
		}

		/* Process row-ii data 1 --> r3-1*/
		for (jj = 1; jj < r3; jj++)
		{
			index = ii*r3+jj;

			if(jj%blockSize==0)
				JJ++;
			realPrecision = pwrErrBound[II*R3+JJ];			
			updateReqLength = 0;			
			
			type_ = type[index];
			if (type_ != 0)
			{
				pred2D = (*data)[index-1] + (*data)[index-r3] - (*data)[index-r3-1];				
				(*data)[index] = pred2D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
			}
			else
			{
				// compute resiBits
				if(updateReqLength==0)
				{
					computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
					reqBytesLength = reqLength/8;
					resiBitsLength = reqLength%8;				
					updateReqLength = 1;
				}

				resiBits = 0;
				if (resiBitsLength != 0) {
					int kMod8 = k % 8;
					int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
					if (rightMovSteps > 0) {
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
					} else if (rightMovSteps < 0) {
						int code1 = getLeftMovingCode(kMod8);
						int code2 = getRightMovingCode(kMod8, resiBitsLength);
						int leftMovSteps = -rightMovSteps;
						rightMovSteps = 8 - leftMovSteps;
						resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
						p++;
						resiBits = resiBits
								| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
					} else // rightMovSteps == 0
					{
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code);
						p++;
					}
					k += resiBitsLength;
				}

				// recover the exact data
				memset(curBytes, 0, 4);
				leadingNum = leadNum[l++];
				memcpy(curBytes, preBytes, leadingNum);
				for (j = leadingNum; j < reqBytesLength; j++)
					curBytes[j] = tdps->exactMidBytes[curByteIndex++];
				if (resiBitsLength != 0) {
					unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
					curBytes[reqBytesLength] = resiByte;
				}

				exactData = bytesToFloat(curBytes);
				(*data)[index] = exactData + medianValue;
				memcpy(preBytes,curBytes,4);
			}
		}
	}

	///////////////////////////	Process layer-1 --> layer-r1-1 ///////////////////////////

	for (kk = 1; kk < r1; kk++)
	{
		/* Process Row-0 data 0*/
		index = kk*r23;		
		if(kk%blockSize==0)
			KK++;
		II = 0;
		JJ = 0;

		realPrecision = pwrErrBound[KK*R23];			
		updateReqLength = 0;			

		type_ = type[index];
		if (type_ != 0)
		{
			pred1D = (*data)[index-r23];			
			(*data)[index] = pred1D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
		}
		else
		{
			// compute resiBits
			if(updateReqLength==0)
			{
				computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
				reqBytesLength = reqLength/8;
				resiBitsLength = reqLength%8;				
				updateReqLength = 1;
			}

			resiBits = 0;
			if (resiBitsLength != 0) {
				int kMod8 = k % 8;
				int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
				if (rightMovSteps > 0) {
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
				} else if (rightMovSteps < 0) {
					int code1 = getLeftMovingCode(kMod8);
					int code2 = getRightMovingCode(kMod8, resiBitsLength);
					int leftMovSteps = -rightMovSteps;
					rightMovSteps = 8 - leftMovSteps;
					resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
					p++;
					resiBits = resiBits
							| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
				} else // rightMovSteps == 0
				{
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code);
					p++;
				}
				k += resiBitsLength;
			}

			// recover the exact data
			memset(curBytes, 0, 4);
			leadingNum = leadNum[l++];
			memcpy(curBytes, preBytes, leadingNum);
			for (j = leadingNum; j < reqBytesLength; j++)
				curBytes[j] = tdps->exactMidBytes[curByteIndex++];
			if (resiBitsLength != 0) {
				unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
				curBytes[reqBytesLength] = resiByte;
			}

			exactData = bytesToFloat(curBytes);
			(*data)[index] = exactData + medianValue;
			memcpy(preBytes,curBytes,4);
		}

		/* Process Row-0 data 1 --> data r3-1 */
		for (jj = 1; jj < r3; jj++)
		{
			index = kk*r23+jj;

			if(jj%blockSize==0)
				JJ++;

			realPrecision = pwrErrBound[KK*R23+JJ];			
			updateReqLength = 0;			
			
			type_ = type[index];
			if (type_ != 0)
			{
				pred2D = (*data)[index-1] + (*data)[index-r23] - (*data)[index-r23-1];			
				(*data)[index] = pred2D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
			}
			else
			{
				// compute resiBits
				if(updateReqLength==0)
				{
					computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
					reqBytesLength = reqLength/8;
					resiBitsLength = reqLength%8;				
					updateReqLength = 1;
				}
			
				resiBits = 0;
				if (resiBitsLength != 0) {
					int kMod8 = k % 8;
					int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
					if (rightMovSteps > 0) {
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
					} else if (rightMovSteps < 0) {
						int code1 = getLeftMovingCode(kMod8);
						int code2 = getRightMovingCode(kMod8, resiBitsLength);
						int leftMovSteps = -rightMovSteps;
						rightMovSteps = 8 - leftMovSteps;
						resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
						p++;
						resiBits = resiBits
								| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
					} else // rightMovSteps == 0
					{
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code);
						p++;
					}
					k += resiBitsLength;
				}

				// recover the exact data
				memset(curBytes, 0, 4);
				leadingNum = leadNum[l++];
				memcpy(curBytes, preBytes, leadingNum);
				for (j = leadingNum; j < reqBytesLength; j++)
					curBytes[j] = tdps->exactMidBytes[curByteIndex++];
				if (resiBitsLength != 0) {
					unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
					curBytes[reqBytesLength] = resiByte;
				}

				exactData = bytesToFloat(curBytes);
				(*data)[index] = exactData + medianValue;
				memcpy(preBytes,curBytes,4);
			}
		}

		/* Process Row-1 --> Row-r2-1 */
		for (ii = 1; ii < r2; ii++)
		{
			/* Process Row-i data 0 */
			index = kk*r23 + ii*r3;
			
			if(ii%blockSize==0)
				II++;
			JJ = 0;
			
			realPrecision = pwrErrBound[KK*R23+II*R3];			
			updateReqLength = 0;						

			type_ = type[index];
			if (type_ != 0)
			{
				pred2D = (*data)[index-r3] + (*data)[index-r23] - (*data)[index-r23-r3];				
				(*data)[index] = pred2D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
			}
			else
			{
				// compute resiBits
				if(updateReqLength==0)
				{
					computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
					reqBytesLength = reqLength/8;
					resiBitsLength = reqLength%8;				
					updateReqLength = 1;
				}

				resiBits = 0;
				if (resiBitsLength != 0) {
					int kMod8 = k % 8;
					int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
					if (rightMovSteps > 0) {
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
					} else if (rightMovSteps < 0) {
						int code1 = getLeftMovingCode(kMod8);
						int code2 = getRightMovingCode(kMod8, resiBitsLength);
						int leftMovSteps = -rightMovSteps;
						rightMovSteps = 8 - leftMovSteps;
						resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
						p++;
						resiBits = resiBits
								| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
					} else // rightMovSteps == 0
					{
						int code = getRightMovingCode(kMod8, resiBitsLength);
						resiBits = (tdps->residualMidBits[p] & code);
						p++;
					}
					k += resiBitsLength;
				}

				// recover the exact data
				memset(curBytes, 0, 4);
				leadingNum = leadNum[l++];
				memcpy(curBytes, preBytes, leadingNum);
				for (j = leadingNum; j < reqBytesLength; j++)
					curBytes[j] = tdps->exactMidBytes[curByteIndex++];
				if (resiBitsLength != 0) {
					unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
					curBytes[reqBytesLength] = resiByte;
				}

				exactData = bytesToFloat(curBytes);
				(*data)[index] = exactData + medianValue;
				memcpy(preBytes,curBytes,4);
			}

			/* Process Row-i data 1 --> data r3-1 */
			for (jj = 1; jj < r3; jj++)
			{
				index = kk*r23 + ii*r3 + jj;
				if(jj%blockSize==0)
					JJ++;

				realPrecision = pwrErrBound[KK*R23+II*R3+JJ];			
				updateReqLength = 0;				

				type_ = type[index];
				if (type_ != 0)
				{
					pred3D = (*data)[index-1] + (*data)[index-r3] + (*data)[index-r23]
					- (*data)[index-r3-1] - (*data)[index-r23-r3] - (*data)[index-r23-1] + (*data)[index-r23-r3-1];					
					(*data)[index] = pred3D + 2 * (type_ - exe_params->intvRadius) * realPrecision;
				}
				else
				{
					// compute resiBits
					if(updateReqLength==0)
					{
						computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
						reqBytesLength = reqLength/8;
						resiBitsLength = reqLength%8;				
						updateReqLength = 1;
					}
				
					resiBits = 0;
					if (resiBitsLength != 0) {
						int kMod8 = k % 8;
						int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
						if (rightMovSteps > 0) {
							int code = getRightMovingCode(kMod8, resiBitsLength);
							resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
						} else if (rightMovSteps < 0) {
							int code1 = getLeftMovingCode(kMod8);
							int code2 = getRightMovingCode(kMod8, resiBitsLength);
							int leftMovSteps = -rightMovSteps;
							rightMovSteps = 8 - leftMovSteps;
							resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
							p++;
							resiBits = resiBits
									| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
						} else // rightMovSteps == 0
						{
							int code = getRightMovingCode(kMod8, resiBitsLength);
							resiBits = (tdps->residualMidBits[p] & code);
							p++;
						}
						k += resiBitsLength;
					}

					// recover the exact data
					memset(curBytes, 0, 4);
					leadingNum = leadNum[l++];
					memcpy(curBytes, preBytes, leadingNum);
					for (j = leadingNum; j < reqBytesLength; j++)
						curBytes[j] = tdps->exactMidBytes[curByteIndex++];
					if (resiBitsLength != 0) {
						unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
						curBytes[reqBytesLength] = resiByte;
					}
					
					exactData = bytesToFloat(curBytes);
					(*data)[index] = exactData + medianValue;
					memcpy(preBytes,curBytes,4);
				}
			}
		}

	}

	free(pwrErrBound);
	free(leadNum);
	free(type);
	return;
}

void decompressDataSeries_float_1D_pwrgroup(float** data, size_t dataSeriesLength, TightDataPointStorageF* tdps) 
{
	float *posGroups, *negGroups, *groups;
	float pos_01_group, neg_01_group;
	int *posFlags, *negFlags;
	
	updateQuantizationInfo(tdps->intervals);
	
	unsigned char* leadNum;
	double interval;// = (float)tdps->realPrecision*2;
	
	convertByteArray2IntArray_fast_2b(tdps->exactDataNum, tdps->leadNumArray, tdps->leadNumArray_size, &leadNum);

	*data = (float*)malloc(sizeof(float)*dataSeriesLength);

	int* type = (int*)malloc(dataSeriesLength*sizeof(int));

#ifdef TIMER__
	huff_cost_start7();
#endif
	if (tdps->entropyType == 0) {
		HuffmanTree* huffmanTree = createHuffmanTree(tdps->stateNum);
		decode_withTree(huffmanTree, tdps->typeArray, dataSeriesLength, type);
		SZ_ReleaseHuffman(huffmanTree);	
	}
	else if (tdps->entropyType == 1) {
		short* type_ = (short*)malloc(dataSeriesLength*sizeof(short));
		int tmpSize = sz_lossless_decompress(confparams_cpr->losslessCompressor, tdps->typeArray, tdps->typeArray_size, (unsigned char **)&type_, dataSeriesLength*sizeof(short));
		if (tmpSize != dataSeriesLength*sizeof(short)){
			printf("tmpSize(%d) != dataSeriesLength*sizeof(short)(%lu)",tmpSize,dataSeriesLength*sizeof(short));
		}
		for (int i=0; i<dataSeriesLength; i++)
			type[i] = (int)type_[i];
		free(type);
	}
	else {
		decode_with_fse(type, dataSeriesLength, tdps->intervals, tdps->FseCode, tdps->FseCode_size, 
					tdps->transCodeBits, tdps->transCodeBits_size);
	}
#ifdef TIMER__
    huff_cost_end7();
    printf("[decoder]: time=%f\n", huffCost7);

#endif
	
	createRangeGroups_float(&posGroups, &negGroups, &posFlags, &negFlags);
	
	float realGroupPrecision;
	float realPrecision = tdps->realPrecision;
	char* groupID = decompressGroupIDArray(tdps->pwrErrBoundBytes, tdps->dataSeriesLength);
	
	//note that the groupID values here are [1,2,3,....,18] or [-1,-2,...,-18]
	
	double* groupErrorBounds = generateGroupErrBounds(confparams_dec->errorBoundMode, realPrecision, confparams_dec->pw_relBoundRatio);
	exe_params->intvRadius = generateGroupMaxIntervalCount(groupErrorBounds);
		
	size_t nbBins = (size_t)(1/confparams_dec->pw_relBoundRatio + 0.5);
	if(nbBins%2==1)
		nbBins++;
	exe_params->intvRadius = nbBins;

	unsigned char preBytes[4];
	unsigned char curBytes[4];
	
	memset(preBytes, 0, 4);

	size_t curByteIndex = 0;
	int reqLength, reqBytesLength = 0, resiBitsLength = 0, resiBits; 
	unsigned char leadingNum;	
	float medianValue, exactData, curValue, predValue;
	
	medianValue = tdps->medianValue;
	
	size_t i, j, k = 0, p = 0, l = 0; // k is to track the location of residual_bit
							// in resiMidBits, p is to track the
							// byte_index of resiMidBits, l is for
							// leadNum
							
	int type_, updateReqLength = 0;
	char rawGrpID = 0, indexGrpID = 0;
	for (i = 0; i < dataSeriesLength; i++) 
	{
		rawGrpID = groupID[i];
		
		if(rawGrpID >= 2)
		{
			groups = posGroups;
			indexGrpID = rawGrpID - 2;
		}
		else if(rawGrpID <= -2)
		{
			groups = negGroups;
			indexGrpID = -rawGrpID - 2;		}
		else if(rawGrpID == 1)
		{
			groups = &pos_01_group;
			indexGrpID = 0;
		}
		else //rawGrpID == -1
		{
			groups = &neg_01_group;
			indexGrpID = 0;			
		}
		
		type_ = type[i];
		switch (type_) {
		case 0:
			// compute resiBits
			if(updateReqLength==0)
			{
				computeReqLength_float(realPrecision, tdps->radExpo, &reqLength, &medianValue);
				reqBytesLength = reqLength/8;
				resiBitsLength = reqLength%8;	
				updateReqLength = 1;	
			}
			resiBits = 0;
			if (resiBitsLength != 0) {
				int kMod8 = k % 8;
				int rightMovSteps = getRightMovingSteps(kMod8, resiBitsLength);
				if (rightMovSteps > 0) {
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code) >> rightMovSteps;
				} else if (rightMovSteps < 0) {
					int code1 = getLeftMovingCode(kMod8);
					int code2 = getRightMovingCode(kMod8, resiBitsLength);
					int leftMovSteps = -rightMovSteps;
					rightMovSteps = 8 - leftMovSteps;
					resiBits = (tdps->residualMidBits[p] & code1) << leftMovSteps;
					p++;
					resiBits = resiBits
							| ((tdps->residualMidBits[p] & code2) >> rightMovSteps);
				} else // rightMovSteps == 0
				{
					int code = getRightMovingCode(kMod8, resiBitsLength);
					resiBits = (tdps->residualMidBits[p] & code);
					p++;
				}
				k += resiBitsLength;
			}

			// recover the exact data	
			memset(curBytes, 0, 4);
			leadingNum = leadNum[l++];
			memcpy(curBytes, preBytes, leadingNum);
			for (j = leadingNum; j < reqBytesLength; j++)
				curBytes[j] = tdps->exactMidBytes[curByteIndex++];
			if (resiBitsLength != 0) {
				unsigned char resiByte = (unsigned char) (resiBits << (8 - resiBitsLength));
				curBytes[reqBytesLength] = resiByte;
			}
			
			exactData = bytesToFloat(curBytes);
			exactData = exactData + medianValue;
			(*data)[i] = exactData;
			memcpy(preBytes,curBytes,4);
			
			groups[indexGrpID] = exactData;
			
			break;
		default:
			predValue = groups[indexGrpID]; //Here, groups[indexGrpID] is the previous value.
			realGroupPrecision = groupErrorBounds[indexGrpID];
			interval = realGroupPrecision*2;		
			
			curValue = predValue + (type_-exe_params->intvRadius)*interval;
			
			//groupNum = computeGroupNum_float(curValue);
			
			if((curValue>0&&rawGrpID<0)||(curValue<0&&rawGrpID>0))
				curValue = 0;
			//else
			//{
			//	realGrpID = fabs(rawGrpID)-2;
			//	if(groupNum<realGrpID)
			//		curValue = rawGrpID>0?pow(2,realGrpID):-pow(2,realGrpID);
			//	else if(groupNum>realGrpID)
			//		curValue = rawGrpID>0?pow(2,groupNum):-pow(2,groupNum);				
			//}	
				
			(*data)[i] = curValue;
			groups[indexGrpID] = curValue;
			break;		
		}
	}	
	
	free(leadNum);
	free(type);
	
	free(posGroups);
	free(negGroups);
	free(posFlags);
	free(negFlags);
	free(groupErrorBounds);
	free(groupID);
}

void decompressDataSeries_float_1D_pwr_pre_log(float** data, size_t dataSeriesLength, TightDataPointStorageF* tdps) {

	decompressDataSeries_float_1D(data, dataSeriesLength, NULL, tdps);
	float threshold = tdps->minLogValue;
	if(tdps->pwrErrBoundBytes_size > 0){
		unsigned char * signs;
		sz_lossless_decompress(ZSTD_COMPRESSOR, tdps->pwrErrBoundBytes, tdps->pwrErrBoundBytes_size, &signs, dataSeriesLength);
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
			else (*data)[i] = exp2((*data)[i]);
			if(signs[i]) (*data)[i] = -((*data)[i]);
		}
		free(signs);
	}
	else{
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
			else (*data)[i] = exp2((*data)[i]);
		}
	}

}

void decompressDataSeries_float_2D_pwr_pre_log(float** data, size_t r1, size_t r2, TightDataPointStorageF* tdps) {

	size_t dataSeriesLength = r1 * r2;
	decompressDataSeries_float_2D(data, r1, r2, NULL, tdps);
	float threshold = tdps->minLogValue;
	if(tdps->pwrErrBoundBytes_size > 0){
		unsigned char * signs;
		sz_lossless_decompress(ZSTD_COMPRESSOR, tdps->pwrErrBoundBytes, tdps->pwrErrBoundBytes_size, &signs, dataSeriesLength);
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
			else (*data)[i] = exp2((*data)[i]);
			if(signs[i]) (*data)[i] = -((*data)[i]);
		}
		free(signs);
	}
	else{
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
			else (*data)[i] = exp2((*data)[i]);
		}
	}

}

void decompressDataSeries_float_3D_pwr_pre_log(float** data, size_t r1, size_t r2, size_t r3, TightDataPointStorageF* tdps) {

	size_t dataSeriesLength = r1 * r2 * r3;
	decompressDataSeries_float_3D(data, r1, r2, r3, NULL, tdps);
	float threshold = tdps->minLogValue;
	if(tdps->pwrErrBoundBytes_size > 0){
		unsigned char * signs;
		sz_lossless_decompress(ZSTD_COMPRESSOR, tdps->pwrErrBoundBytes, tdps->pwrErrBoundBytes_size, &signs, dataSeriesLength);
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
			else (*data)[i] = exp2((*data)[i]);
			if(signs[i]) (*data)[i] = -((*data)[i]);
		}
		free(signs);
	}
	else{
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
			else (*data)[i] = exp2((*data)[i]);
		}
	}
}


void decompressDataSeries_float_1D_pwr_pre_log_MSST19(float** data, size_t dataSeriesLength, TightDataPointStorageF* tdps) 
{
	decompressDataSeries_float_1D_MSST19(data, dataSeriesLength, tdps);
	float threshold = tdps->minLogValue;
	uint32_t* ptr;

	if(tdps->pwrErrBoundBytes_size > 0){
		unsigned char * signs = NULL;
		if(tdps->pwrErrBoundBytes_size==0)
		{
			signs = (unsigned char*)malloc(dataSeriesLength);
			memset(signs, 0, dataSeriesLength);
		}
		else
			sz_lossless_decompress(ZSTD_COMPRESSOR, tdps->pwrErrBoundBytes, tdps->pwrErrBoundBytes_size, &signs, dataSeriesLength);
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold && (*data)[i] >= 0){
				(*data)[i] = 0;
				continue;
			}
			if(signs[i]){
			    ptr = (uint32_t*)(*data) + i;
				*ptr |= 0x80000000;
			}
		}
		free(signs);
	}
	else{
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
		}
	}
}

void decompressDataSeries_float_2D_pwr_pre_log_MSST19(float** data, size_t r1, size_t r2, TightDataPointStorageF* tdps) {

	size_t dataSeriesLength = r1 * r2;
	decompressDataSeries_float_2D_MSST19(data, r1, r2, tdps);
	float threshold = tdps->minLogValue;
	uint32_t* ptr;

	if(tdps->pwrErrBoundBytes_size > 0){
		unsigned char * signs;
		if(tdps->pwrErrBoundBytes_size==0)
		{
			signs = (unsigned char*)malloc(dataSeriesLength);
			memset(signs, 0, dataSeriesLength);
		}
		else
			sz_lossless_decompress(ZSTD_COMPRESSOR, tdps->pwrErrBoundBytes, tdps->pwrErrBoundBytes_size, &signs, dataSeriesLength);
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold && (*data)[i] >= 0){
				(*data)[i] = 0;
				continue;
			}
			if(signs[i]){
				ptr = (uint32_t*)(*data) + i;
				*ptr |= 0x80000000;
			}
		}
		free(signs);
	}
	else{
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
		}
	}
}

void decompressDataSeries_float_3D_pwr_pre_log_MSST19(float** data, size_t r1, size_t r2, size_t r3, TightDataPointStorageF* tdps) {

	size_t dataSeriesLength = r1 * r2 * r3;
	decompressDataSeries_float_3D_MSST19(data, r1, r2, r3, tdps);
	float threshold = tdps->minLogValue;
	if(tdps->pwrErrBoundBytes_size > 0){
		unsigned char * signs;
		uint32_t* ptr;
		if(tdps->pwrErrBoundBytes_size==0)
		{
			signs = (unsigned char*)malloc(dataSeriesLength);
			memset(signs, 0, dataSeriesLength);
		}
		else
			sz_lossless_decompress(ZSTD_COMPRESSOR, tdps->pwrErrBoundBytes, tdps->pwrErrBoundBytes_size, &signs, dataSeriesLength);
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold && (*data)[i] >= 0) {
			    (*data)[i] = 0;
                continue;
			}
			if(signs[i]) {
			    ptr = (uint32_t*)(*data)+i;
			    *ptr |= 0x80000000;
			}
		}
		free(signs);
	}
	else{
		for(size_t i=0; i<dataSeriesLength; i++){
			if((*data)[i] < threshold) (*data)[i] = 0;
		}
	}
}

#pragma GCC diagnostic pop
