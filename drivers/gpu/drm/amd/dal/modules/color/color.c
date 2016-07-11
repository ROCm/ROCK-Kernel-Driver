/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "dc.h"
#include "mod_color.h"
#include "core_types.h"
#include "fixed31_32.h"

#define MOD_COLOR_MAX_CONCURRENT_SINKS 32

struct sink_caps {
	const struct dc_sink *sink;
};

const unsigned int gamut_divider = 10000;

struct gamut_calculation_matrix {
	struct fixed31_32 *MTransposed;
	struct fixed31_32 *XYZtoRGB_Custom;
	struct fixed31_32 *XYZtoRGB_Ref;
	struct fixed31_32 *RGBtoXYZ_Final;

	struct fixed31_32 *MResult;
	struct fixed31_32 *fXYZofWhiteRef;
	struct fixed31_32 *fXYZofRGBRef;
};

struct gamut_src_dst_matrix {
	struct fixed31_32 *rgbCoeffDst;
	struct fixed31_32 *whiteCoeffDst;
	struct fixed31_32 *rgbCoeffSrc;
	struct fixed31_32 *whiteCoeffSrc;
};


struct core_color {
	struct mod_color public;
	struct dc *dc;
	struct sink_caps *caps;
	int num_sinks;
	bool *user_enable_color_temperature;
	int color_temperature;
	struct color_space_coordinates *source_gamut;
	struct color_space_coordinates *destination_gamut;
};

#define MOD_COLOR_TO_CORE(mod_color)\
		container_of(mod_color, struct core_color, public)

/*Matrix Calculation Functions*/
/**
 *****************************************************************************
 *  Function: transposeMatrix
 *
 *  @brief
 *    rotate the matrix 90 degrees clockwise
 *    rows become a columns and columns to rows
 *  @param [ in ] M            - source matrix
 *  @param [ in ] Rows         - num of Rows of the original matrix
 *  @param [ in ] Cols         - num of Cols of the original matrix
 *  @param [ out] MTransposed  - result matrix
 *  @return  void
 *
 *****************************************************************************
 */
static void transpose_matrix(const struct fixed31_32 *M, unsigned int Rows,
		unsigned int Cols,  struct fixed31_32 *MTransposed)
{
	unsigned int i, j;

	for (i = 0; i < Rows; i++) {
		for (j = 0; j < Cols; j++)
			MTransposed[(j*Rows)+i] = M[(i*Cols)+j];
	}
}

/**
 *****************************************************************************
 *  Function: multiplyMatrices
 *
 *  @brief
 *    multiplies produce of two matrices: M =  M1[ulRows1 x ulCols1] *
 *    M2[ulCols1 x ulCols2].
 *
 *  @param [ in ] M1      - first Matrix.
 *  @param [ in ] M2      - second Matrix.
 *  @param [ in ] Rows1   - num of Rows of the first Matrix
 *  @param [ in ] Cols1   - num of Cols of the first Matrix/Num of Rows
 *  of the second Matrix
 *  @param [ in ] Cols2   - num of Cols of the second Matrix
 *  @param [out ] mResult - resulting matrix.
 *  @return  void
 *
 *****************************************************************************
 */
static void multiply_matrices(struct fixed31_32 *mResult,
		const struct fixed31_32 *M1,
		const struct fixed31_32 *M2, unsigned int Rows1,
		unsigned int Cols1, unsigned int Cols2)
{
	unsigned int i, j, k;

	for (i = 0; i < Rows1; i++) {
		for (j = 0; j < Cols2; j++) {
			mResult[(i * Cols2) + j] = dal_fixed31_32_zero;
			for (k = 0; k < Cols1; k++)
				mResult[(i * Cols2) + j] =
					dal_fixed31_32_add
					(mResult[(i * Cols2) + j],
					dal_fixed31_32_mul(M1[(i * Cols1) + k],
					M2[(k * Cols2) + j]));
		}
	}
}

/**
 *****************************************************************************
 *  Function: cFind3X3Det
 *
 *  @brief
 *    finds determinant of given 3x3 matrix
 *
 *  @param [ in  ] m     - matrix
 *  @return determinate whioch could not be zero
 *
 *****************************************************************************
 */
static struct fixed31_32 find_3X3_det(const struct fixed31_32 *m)
{
	struct fixed31_32 det, A1, A2, A3;

	A1 = dal_fixed31_32_mul(m[0],
			dal_fixed31_32_sub(dal_fixed31_32_mul(m[4], m[8]),
					dal_fixed31_32_mul(m[5], m[7])));
	A2 = dal_fixed31_32_mul(m[1],
			dal_fixed31_32_sub(dal_fixed31_32_mul(m[3], m[8]),
					dal_fixed31_32_mul(m[5], m[6])));
	A3 = dal_fixed31_32_mul(m[2],
			dal_fixed31_32_sub(dal_fixed31_32_mul(m[3], m[7]),
					dal_fixed31_32_mul(m[4], m[6])));
	det = dal_fixed31_32_add(dal_fixed31_32_sub(A1, A2), A3);
	return det;
}


/**
 *****************************************************************************
 *  Function: computeInverseMatrix_3x3
 *
 *  @brief
 *    builds inverse matrix
 *
 *  @param [ in   ] m     - matrix
 *  @param [ out  ] im    - result matrix
 *  @return true if success
 *
 *****************************************************************************
 */
static bool compute_inverse_matrix_3x3(const struct fixed31_32 *m,
		struct fixed31_32 *im)
{
	struct fixed31_32 determinant = find_3X3_det(m);

	if (dal_fixed31_32_eq(determinant, dal_fixed31_32_zero) == false) {
		im[0] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[4], m[8]),
				dal_fixed31_32_mul(m[5], m[7])), determinant);
		im[1] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[1], m[8]),
				dal_fixed31_32_mul(m[2], m[7])), determinant));
		im[2] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[1], m[5]),
				dal_fixed31_32_mul(m[2], m[4])), determinant);
		im[3] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[3], m[8]),
				dal_fixed31_32_mul(m[5], m[6])), determinant));
		im[4] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[8]),
				dal_fixed31_32_mul(m[2], m[6])), determinant);
		im[5] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[5]),
				dal_fixed31_32_mul(m[2], m[3])), determinant));
		im[6] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[3], m[7]),
				dal_fixed31_32_mul(m[4], m[6])), determinant);
		im[7] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[7]),
				dal_fixed31_32_mul(m[1], m[6])), determinant));
		im[8] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[4]),
				dal_fixed31_32_mul(m[1], m[3])), determinant);
		return true;
	}
	return false;
}


/**
 *****************************************************************************
 *  Function: calculateXYZtoRGB_M3x3
 *
 *  @brief
 *    Calculates transformation matrix from XYZ coordinates to RBG
 *
 *  @param [ in  ] XYZofRGB     - primaries XYZ
 *  @param [ in  ] XYZofWhite   - white point.
 *  @param [ out ] XYZtoRGB     - RGB primires
 *  @return  true if success
 *
 *****************************************************************************
 */
static bool calculate_XYZ_to_RGB_3x3(const struct fixed31_32 *XYZofRGB,
		const struct fixed31_32 *XYZofWhite,
		struct fixed31_32 *XYZtoRGB)
{

	struct fixed31_32 MInversed[9];
	struct fixed31_32 SVector[3];

	/*1. Find Inverse matrix 3x3 of MTransposed*/
	if (!compute_inverse_matrix_3x3(XYZofRGB, MInversed))
	return false;

	/*2. Calculate vector: |Sr Sg Sb| = [MInversed] * |Wx Wy Wz|*/
	multiply_matrices(SVector, MInversed, XYZofWhite, 3, 3, 1);

	/*3. Calculate matrix XYZtoRGB 3x3*/
	XYZtoRGB[0] = dal_fixed31_32_mul(XYZofRGB[0], SVector[0]);
	XYZtoRGB[1] = dal_fixed31_32_mul(XYZofRGB[1], SVector[1]);
	XYZtoRGB[2] = dal_fixed31_32_mul(XYZofRGB[2], SVector[2]);

	XYZtoRGB[3] = dal_fixed31_32_mul(XYZofRGB[3], SVector[0]);
	XYZtoRGB[4] = dal_fixed31_32_mul(XYZofRGB[4], SVector[1]);
	XYZtoRGB[5] = dal_fixed31_32_mul(XYZofRGB[5], SVector[2]);

	XYZtoRGB[6] = dal_fixed31_32_mul(XYZofRGB[6], SVector[0]);
	XYZtoRGB[7] = dal_fixed31_32_mul(XYZofRGB[7], SVector[1]);
	XYZtoRGB[8] = dal_fixed31_32_mul(XYZofRGB[8], SVector[2]);

	return true;
}


/**
*****************************************************************************
*  Function: allocateCalculationMatrix
*
*  @brief
*     allocates several arrays of floating point pointers
*     save several allocations in one call
*  @param [out  ] pMatrix - holds internal pointers of allocated arrays
*  @return  true if success
*
*
*****************************************************************************
*/

static bool allocate_calculation_matrix
		(struct gamut_calculation_matrix *pMatrix)
{
	unsigned int sizeOfAllocation =
			sizeof(struct fixed31_32) * 9 * 6 +
			sizeof(struct fixed31_32) * 3;

	pMatrix->MTransposed =
			(struct fixed31_32 *)(dm_alloc(sizeOfAllocation));

	if (pMatrix->MTransposed != NULL) {
		pMatrix->XYZtoRGB_Custom = pMatrix->MTransposed + 9;
		pMatrix->XYZtoRGB_Ref = pMatrix->XYZtoRGB_Custom + 9;
		pMatrix->RGBtoXYZ_Final = pMatrix->XYZtoRGB_Ref + 9;
		pMatrix->MResult = pMatrix->RGBtoXYZ_Final + 9;
		pMatrix->fXYZofWhiteRef = pMatrix->MResult + 9;
		pMatrix->fXYZofRGBRef = pMatrix->fXYZofWhiteRef + 3;

		return true;
	}

	return false;
}

/*****************************************************************************
*  Function: deallocateCalculationMatrix
*
*  @brief
*     free allocated pointer
*  @param [in  ] pMatrix - holds internal pointer required to free
*
*
*****************************************************************************
*/
static void deallocate_calculation_matrix
		(struct gamut_calculation_matrix *pMatrix)
{
	if (pMatrix->MTransposed != NULL) {
		dm_free(pMatrix->MTransposed);
		pMatrix->MTransposed = NULL;
	}
}

/**
 *****************************************************************************
 *  Function: allocateMatrix
 *
 *  @brief
 *      allocate array of pointers
 *  @param [out  ] pMatrix - holds pointers for allocation
 *  @return  true if success
 *
 *****************************************************************************
 */

static bool allocate_matrix(struct gamut_src_dst_matrix *pMatrix)
{
	const unsigned int sizeOfAllocation =
			(sizeof(struct fixed31_32) * 3 +
			sizeof(struct fixed31_32) * 9) * 2;

	pMatrix->rgbCoeffDst =
			(struct fixed31_32 *)(dm_alloc(sizeOfAllocation));

	if (pMatrix->rgbCoeffDst != NULL) {
		pMatrix->whiteCoeffDst = pMatrix->rgbCoeffDst + 9;
		pMatrix->rgbCoeffSrc = pMatrix->whiteCoeffDst + 3;
		pMatrix->whiteCoeffSrc = pMatrix->rgbCoeffSrc + 9;

		return true;
	}

	return false;
}
/**
 *****************************************************************************
 *  Function: deallocateMatrix
 *
 *  @brief
 *      frees array of pointers
 *  @param [out  ] pMatrix - holds pointers for reelase
 *  @return  void
 *
 *****************************************************************************
 */
static void deallocate_matrix(struct gamut_src_dst_matrix *pMatrix)
{
	if (pMatrix->rgbCoeffDst != NULL) {
		dm_free(pMatrix->rgbCoeffDst);
		pMatrix->rgbCoeffDst = NULL;
	}
}

static bool gamut_to_color_matrix(
	const struct fixed31_32 *pXYZofRGB,/*destination gamut*/
	const struct fixed31_32 *pXYZofWhite,/*destination of white point*/
	const struct fixed31_32 *pRefXYZofRGB,/*source gamut*/
	const struct fixed31_32 *pRefXYZofWhite,/*source of white point*/
	bool invert,
	struct fixed31_32 *tempMatrix3X3)
{
	int i = 0;
	struct gamut_calculation_matrix matrix;
	/*pointers used as assignment vars, not allocation*/
	struct fixed31_32 *pXYZtoRGB_Temp;
	struct fixed31_32 *pXYZtoRGB_Final;

	matrix.MTransposed = NULL;

	if (!allocate_calculation_matrix(&matrix))
		return false;

	matrix.fXYZofWhiteRef[0] = pRefXYZofWhite[0];
	matrix.fXYZofWhiteRef[1] = pRefXYZofWhite[1];
	matrix.fXYZofWhiteRef[2] = pRefXYZofWhite[2];


	matrix.fXYZofRGBRef[0] = pRefXYZofRGB[0];
	matrix.fXYZofRGBRef[1] = pRefXYZofRGB[1];
	matrix.fXYZofRGBRef[2] = pRefXYZofRGB[2];

	matrix.fXYZofRGBRef[3] = pRefXYZofRGB[3];
	matrix.fXYZofRGBRef[4] = pRefXYZofRGB[4];
	matrix.fXYZofRGBRef[5] = pRefXYZofRGB[5];

	matrix.fXYZofRGBRef[6] = pRefXYZofRGB[6];
	matrix.fXYZofRGBRef[7] = pRefXYZofRGB[7];
	matrix.fXYZofRGBRef[8] = pRefXYZofRGB[8];

	/*default values -  unity matrix*/
	while (i < 9) {
		if (i == 0 || i == 4 || i == 8)
			tempMatrix3X3[i] = dal_fixed31_32_one;
		else
			tempMatrix3X3[i] = dal_fixed31_32_zero;
		i++;
	}

	/*1. Decide about the order of calculation.
	 * bInvert == FALSE --> RGBtoXYZ_Ref * XYZtoRGB_Custom
	 * bInvert == TRUE  --> RGBtoXYZ_Custom * XYZtoRGB_Ref */
	if (invert) {
		pXYZtoRGB_Temp  = matrix.XYZtoRGB_Custom;
		pXYZtoRGB_Final = matrix.XYZtoRGB_Ref;
	} else {
		pXYZtoRGB_Temp  = matrix.XYZtoRGB_Ref;
		pXYZtoRGB_Final = matrix.XYZtoRGB_Custom;
	}

	/*2. Calculate XYZtoRGB_Ref*/
	transpose_matrix(matrix.fXYZofRGBRef, 3, 3, matrix.MTransposed);

	if (!calculate_XYZ_to_RGB_3x3(
		matrix.MTransposed,
		matrix.fXYZofWhiteRef,
		matrix.XYZtoRGB_Ref))
		goto calculation_failure;

	/*3. Calculate XYZtoRGB_Custom*/
	transpose_matrix(pXYZofRGB, 3, 3, matrix.MTransposed);

	if (!calculate_XYZ_to_RGB_3x3(
		matrix.MTransposed,
		pXYZofWhite,
		matrix.XYZtoRGB_Custom))
		goto calculation_failure;

	/*4. Calculate RGBtoXYZ -
	 * inverse matrix 3x3 of XYZtoRGB_Ref or XYZtoRGB_Custom*/
	if (!compute_inverse_matrix_3x3(pXYZtoRGB_Temp, matrix.RGBtoXYZ_Final))
		goto calculation_failure;

	/*5. Calculate M(3x3) = RGBtoXYZ * XYZtoRGB*/
	multiply_matrices(matrix.MResult, matrix.RGBtoXYZ_Final,
			pXYZtoRGB_Final, 3, 3, 3);

	for (i = 0; i < 9; i++)
		tempMatrix3X3[i] = matrix.MResult[i];

	deallocate_calculation_matrix(&matrix);

	return true;

calculation_failure:
	deallocate_calculation_matrix(&matrix);
	return false;
}

static bool build_gamut_remap_matrix
		(struct color_space_coordinates gamut_description,
		struct fixed31_32 *rgb_matrix,
		struct fixed31_32 *white_point_matrix)
{
	struct fixed31_32 fixed_blueX = dal_fixed31_32_from_fraction
			(gamut_description.blueX, gamut_divider);
	struct fixed31_32 fixed_blueY = dal_fixed31_32_from_fraction
			(gamut_description.blueY, gamut_divider);
	struct fixed31_32 fixed_greenX = dal_fixed31_32_from_fraction
			(gamut_description.greenX, gamut_divider);
	struct fixed31_32 fixed_greenY = dal_fixed31_32_from_fraction
			(gamut_description.greenY, gamut_divider);
	struct fixed31_32 fixed_redX = dal_fixed31_32_from_fraction
			(gamut_description.redX, gamut_divider);
	struct fixed31_32 fixed_redY = dal_fixed31_32_from_fraction
			(gamut_description.redY, gamut_divider);
	struct fixed31_32 fixed_whiteX = dal_fixed31_32_from_fraction
			(gamut_description.whiteX, gamut_divider);
	struct fixed31_32 fixed_whiteY = dal_fixed31_32_from_fraction
			(gamut_description.whiteY, gamut_divider);

	rgb_matrix[0] = dal_fixed31_32_div(fixed_redX, fixed_redY);
	rgb_matrix[1] = dal_fixed31_32_one;
	rgb_matrix[2] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_redX),
					fixed_redY), fixed_redY);

	rgb_matrix[3] = dal_fixed31_32_div(fixed_greenX, fixed_greenY);
	rgb_matrix[4] = dal_fixed31_32_one;
	rgb_matrix[5] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_greenX),
					fixed_greenY), fixed_greenY);

	rgb_matrix[6] = dal_fixed31_32_div(fixed_blueX, fixed_blueY);
	rgb_matrix[7] = dal_fixed31_32_one;
	rgb_matrix[8] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_blueX),
					fixed_blueY), fixed_blueY);

	white_point_matrix[0] = dal_fixed31_32_div(fixed_whiteX, fixed_whiteY);
	white_point_matrix[1] = dal_fixed31_32_one;
	white_point_matrix[2] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_whiteX),
					fixed_whiteY), fixed_whiteY);

	return true;
}

static bool check_dc_support(const struct dc *dc)
{
	if (dc->stream_funcs.set_gamut_remap == NULL)
		return false;

	return true;
}

struct mod_color *mod_color_create(struct dc *dc)
{
	int i = 0;
	struct core_color *core_color =
				dm_alloc(sizeof(struct core_color));
	if (core_color == NULL)
		goto fail_alloc_context;

	core_color->caps = dm_alloc(sizeof(struct sink_caps) *
			MOD_COLOR_MAX_CONCURRENT_SINKS);

	if (core_color->caps == NULL)
		goto fail_alloc_caps;

	for (i = 0; i < MOD_COLOR_MAX_CONCURRENT_SINKS; i++)
		core_color->caps[i].sink = NULL;

	core_color->user_enable_color_temperature =
			dm_alloc(sizeof(bool) *
					MOD_COLOR_MAX_CONCURRENT_SINKS);

	if (core_color->user_enable_color_temperature == NULL)
		goto fail_alloc_user_enable;

	core_color->num_sinks = 0;
	core_color->color_temperature = 6500;

	core_color->source_gamut =
			dm_alloc(sizeof(struct color_space_coordinates) *
					MOD_COLOR_MAX_CONCURRENT_SINKS);

	if (core_color->source_gamut == NULL)
		goto fail_alloc_source_gamut;

	for (i = 0; i < MOD_COLOR_MAX_CONCURRENT_SINKS; i++) {
		core_color->source_gamut[i].blueX = 1500;
		core_color->source_gamut[i].blueY = 600;
		core_color->source_gamut[i].greenX = 3000;
		core_color->source_gamut[i].greenY = 6000;
		core_color->source_gamut[i].redX = 6400;
		core_color->source_gamut[i].redY = 3300;
		core_color->source_gamut[i].whiteX = 3127;
		core_color->source_gamut[i].whiteY = 3290;
	}

	core_color->destination_gamut =
			dm_alloc(sizeof(struct color_space_coordinates) *
					MOD_COLOR_MAX_CONCURRENT_SINKS);

	if (core_color->destination_gamut == NULL)
			goto fail_alloc_destination_gamut;

	for (i = 0; i < MOD_COLOR_MAX_CONCURRENT_SINKS; i++) {
		core_color->destination_gamut[i].blueX = 1500;
		core_color->destination_gamut[i].blueY = 600;
		core_color->destination_gamut[i].greenX = 3000;
		core_color->destination_gamut[i].greenY = 6000;
		core_color->destination_gamut[i].redX = 6400;
		core_color->destination_gamut[i].redY = 3300;
		core_color->destination_gamut[i].whiteX = 3127;
		core_color->destination_gamut[i].whiteY = 3290;
	}

	if (dc == NULL)
		goto fail_construct;

	core_color->dc = dc;

	if (!check_dc_support(dc))
		goto fail_construct;

	return &core_color->public;

fail_construct:
	dm_free(core_color->destination_gamut);

fail_alloc_destination_gamut:
	dm_free(core_color->source_gamut);

fail_alloc_source_gamut:
	dm_free(core_color->user_enable_color_temperature);

fail_alloc_user_enable:
	dm_free(core_color->caps);

fail_alloc_caps:
	dm_free(core_color);

fail_alloc_context:
	return NULL;
}

void mod_color_destroy(struct mod_color *mod_color)
{
	if (mod_color != NULL) {
		int i;
		struct core_color *core_color =
				MOD_COLOR_TO_CORE(mod_color);

		dm_free(core_color->destination_gamut);

		dm_free(core_color->source_gamut);

		dm_free(core_color->user_enable_color_temperature);

		for (i = 0; i < core_color->num_sinks; i++)
			dc_sink_release(core_color->caps[i].sink);

		dm_free(core_color->caps);

		dm_free(core_color);
	}
}

/* Given a specific dc_sink* this function finds its equivalent
 * on the dc_sink array and returns the corresponding index
 */
static unsigned int sink_index_from_sink(struct core_color *core_color,
		const struct dc_sink *sink)
{
	unsigned int index = 0;

	for (index = 0; index < core_color->num_sinks; index++)
		if (core_color->caps[index].sink == sink)
			return index;

	/* Could not find sink requested */
	ASSERT(false);
	return index;
}


bool mod_color_adjust_temperature(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int temperature)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	/*TODO: calculations for the matrix and add to the streams*/
	struct fixed31_32 identity_matrix[12];

	identity_matrix[0] = dal_fixed31_32_from_fraction(temperature, 6500);
	identity_matrix[1] = dal_fixed31_32_zero;
	identity_matrix[2] = dal_fixed31_32_zero;
	identity_matrix[3] = dal_fixed31_32_zero;
	identity_matrix[4] = dal_fixed31_32_zero;
	identity_matrix[5] = dal_fixed31_32_from_fraction(temperature, 6500);
	identity_matrix[6] = dal_fixed31_32_zero;
	identity_matrix[7] = dal_fixed31_32_zero;
	identity_matrix[8] = dal_fixed31_32_zero;
	identity_matrix[9] = dal_fixed31_32_zero;
	identity_matrix[10] = dal_fixed31_32_from_fraction(temperature, 6500);
	identity_matrix[11] = dal_fixed31_32_zero;

	int i;
	int j;

	for (i = 0; i < num_streams; i++) {
		struct core_stream *core_stream = DC_STREAM_TO_CORE(streams[i]);

		core_stream->public.gamut_remap_matrix.enable_remap = true;

		for (j = 0; j < 12; j++)
			core_stream->public.gamut_remap_matrix.matrix[j] =
					identity_matrix[j];
	}

	core_color->dc->stream_funcs.set_gamut_remap(core_color->dc, streams,
						num_streams);

	return true;
}

bool mod_color_adjust_source_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct gamut_space_coordinates inputGamutCoord,
		struct white_point_coodinates inputWhitePointCoord)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	core_color->source_gamut[0].blueX = inputGamutCoord.blueX;
	core_color->source_gamut[0].blueY = inputGamutCoord.blueY;
	core_color->source_gamut[0].greenX = inputGamutCoord.greenX;
	core_color->source_gamut[0].greenY = inputGamutCoord.greenY;
	core_color->source_gamut[0].redX = inputGamutCoord.redX;
	core_color->source_gamut[0].redY = inputGamutCoord.redY;
	core_color->source_gamut[0].whiteX = inputWhitePointCoord.whiteX;
	core_color->source_gamut[0].whiteY = inputWhitePointCoord.whiteY;

	struct gamut_src_dst_matrix matrix;

	if (!allocate_matrix(&matrix))
		return false;

	if (!build_gamut_remap_matrix(core_color->source_gamut[0],
			matrix.rgbCoeffSrc, matrix.whiteCoeffSrc))
		return false;

	if (!build_gamut_remap_matrix(core_color->destination_gamut[0],
				matrix.rgbCoeffDst, matrix.whiteCoeffDst))
		return false;

	struct fixed31_32 temp_matrix[9];

	if (!gamut_to_color_matrix(
			matrix.rgbCoeffDst,
			matrix.whiteCoeffDst,
			matrix.rgbCoeffSrc,
			matrix.whiteCoeffSrc,
			true,
			temp_matrix))
		return false;

	/*TODO: calculations for the matrix and add to the streams*/
	struct fixed31_32 identity_matrix[12];

	identity_matrix[0] = temp_matrix[0];
	identity_matrix[1] = temp_matrix[1];
	identity_matrix[2] = temp_matrix[2];
	identity_matrix[3] = matrix.whiteCoeffSrc[0];
	identity_matrix[4] = temp_matrix[3];
	identity_matrix[5] = temp_matrix[4];
	identity_matrix[6] = temp_matrix[5];
	identity_matrix[7] = matrix.whiteCoeffSrc[1];
	identity_matrix[8] = temp_matrix[6];
	identity_matrix[9] = temp_matrix[7];
	identity_matrix[10] = temp_matrix[8];
	identity_matrix[11] = matrix.whiteCoeffSrc[2];

	int i;
	int j;

	for (i = 0; i < num_streams; i++) {
		struct core_stream *core_stream = DC_STREAM_TO_CORE(streams[i]);

		core_stream->public.gamut_remap_matrix.enable_remap = true;

		for (j = 0; j < 12; j++)
			core_stream->public.gamut_remap_matrix.matrix[j] =
					identity_matrix[j];
	}

	core_color->dc->stream_funcs.set_gamut_remap(core_color->dc, streams,
						num_streams);

	return true;
}

bool mod_color_adjust_destination_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct gamut_space_coordinates inputGamutCoord,
		struct white_point_coodinates inputWhitePointCoord)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	core_color->destination_gamut[0].blueX = inputGamutCoord.blueX;
	core_color->destination_gamut[0].blueY = inputGamutCoord.blueY;
	core_color->destination_gamut[0].greenX = inputGamutCoord.greenX;
	core_color->destination_gamut[0].greenY = inputGamutCoord.greenY;
	core_color->destination_gamut[0].redX = inputGamutCoord.redX;
	core_color->destination_gamut[0].redY = inputGamutCoord.redY;
	core_color->destination_gamut[0].whiteX = inputWhitePointCoord.whiteX;
	core_color->destination_gamut[0].whiteY = inputWhitePointCoord.whiteY;

	struct gamut_src_dst_matrix matrix;

	if (!allocate_matrix(&matrix))
		return false;

	if (!build_gamut_remap_matrix(core_color->source_gamut[0],
			matrix.rgbCoeffSrc, matrix.whiteCoeffSrc))
		return false;

	if (!build_gamut_remap_matrix(core_color->destination_gamut[0],
				matrix.rgbCoeffDst, matrix.whiteCoeffDst))
		return false;

	struct fixed31_32 temp_matrix[9];

	if (!gamut_to_color_matrix(
			matrix.rgbCoeffDst,
			matrix.whiteCoeffDst,
			matrix.rgbCoeffSrc,
			matrix.whiteCoeffSrc,
			true,
			temp_matrix))
		return false;

	/*TODO: calculations for the matrix and add to the streams*/
	struct fixed31_32 identity_matrix[12];

	identity_matrix[0] = temp_matrix[0];
	identity_matrix[1] = temp_matrix[1];
	identity_matrix[2] = temp_matrix[2];
	identity_matrix[3] = matrix.whiteCoeffDst[0];
	identity_matrix[4] = temp_matrix[3];
	identity_matrix[5] = temp_matrix[4];
	identity_matrix[6] = temp_matrix[5];
	identity_matrix[7] = matrix.whiteCoeffDst[1];
	identity_matrix[8] = temp_matrix[6];
	identity_matrix[9] = temp_matrix[7];
	identity_matrix[10] = temp_matrix[8];
	identity_matrix[11] = matrix.whiteCoeffDst[2];

	int i;
	int j;

	for (i = 0; i < num_streams; i++) {
		struct core_stream *core_stream = DC_STREAM_TO_CORE(streams[i]);

		core_stream->public.gamut_remap_matrix.enable_remap = true;

		for (j = 0; j < 12; j++)
			core_stream->public.gamut_remap_matrix.matrix[j] =
					identity_matrix[j];
	}

	core_color->dc->stream_funcs.set_gamut_remap(core_color->dc, streams,
						num_streams);

	return true;
}

/*TODO: user enable for color temperature button*/

bool mod_color_set_user_enable(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		bool user_enable)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {

		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		core_color->user_enable_color_temperature[sink_index] =
				user_enable;
	}

	/*set_color_on_streams(core_color, streams, num_streams);*/

	return true;
}




