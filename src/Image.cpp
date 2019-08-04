#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define PROC_IMG_WIDTH INPUT_IMG_WIDTH
#define PROC_IMG_HEIGHT INPUT_IMG_HEIGHT

#if defined(SCAN)

#include <gsl/gsl_multifit.h>
#include <boost/format.hpp>
#include "alphacode.h"
#include "Image.h"
#include <iostream>

extern void gsl_error_handler(const char * reason, const char * file, int line, int gsl_errno);
int read_int(FILE *fp);

Image::Image(size_t N_)
		: N(N_)
{
	if (PROC_IMG_WIDTH < PROC_IMG_HEIGHT) {
		std::cerr << "image width must be greater than image height" << std::endl;
		exit(-1);
	}

	if (N > MAX_N) {
		std::cerr << "N exceeds maximum allowed value " << MAX_N << std::endl;
		exit(-1);
	}

#if defined(GUI)
	src = cvCreateImage(cvSize(PROC_IMG_WIDTH, PROC_IMG_HEIGHT), IPL_DEPTH_8U, 1);
	binarized = cvCreateImage(cvSize(PROC_IMG_WIDTH, PROC_IMG_HEIGHT), IPL_DEPTH_8U, 1);
#endif
//	smooth = cvCreateImage(cvSize(PROC_IMG_WIDTH, PROC_IMG_HEIGHT), IPL_DEPTH_8U, 1);
//	morphed = cvCreateImage(cvSize(PROC_IMG_WIDTH, PROC_IMG_HEIGHT), IPL_DEPTH_8U, 1);

	cov = gsl_matrix_alloc(p, p);
	c = gsl_vector_alloc(p);
	gsl_set_error_handler(gsl_error_handler);
	for (size_t i = p; i < sizeof(work_) / sizeof(work_[0]); i++) {
		X_[i] = gsl_matrix_alloc(i, p);
		for (size_t j = 0; j < i; j++)
			gsl_matrix_set(X_[i], j, 0, 1.0);
		y_[i] = gsl_vector_alloc(i);
		work_[i] = gsl_multifit_robust_alloc(gsl_multifit_robust_bisquare, X_[i]->size1, X_[i]->size2);
	}

	dstAnchors[0].x = CLIPPED_IMG_WIDTH - 1;
	dstAnchors[0].y = 0;
	dstAnchors[1].x = CLIPPED_IMG_WIDTH - 1;
	dstAnchors[1].y = CLIPPED_IMG_HEIGHT - 1;
	dstAnchors[2].x = 0;
	dstAnchors[2].y = 0;

	affineMat = cvCreateMat(2, 3, CV_32FC1); // affine transformation matrix

	/* load match filters */
	const int L = CLIPPED_IMG_WIDTH * CLIPPED_IMG_WIDTH;
	for (size_t c = 0; c < N; c++) {
		for (int a = 0; a < MAX_ALT_SHAPES; a++) {
			std::stringstream ss;
			ss << boost::format("%02X") % (int) AlphaCode::chars[c];
			if (a)
				ss << '-' << (a + 1);
			FILE *fp = fopen(ss.str().c_str(), "r");
			if (!fp) {
				sym[c][a] = 0;
				break;
			}
			sym[c][a] = new int[CLIPPED_IMG_WIDTH * CLIPPED_IMG_WIDTH];
			for (int i = 0; i < L; i++)
				sym[c][a][i] = read_int(fp);
			fclose(fp);
		}
		sym[c][MAX_ALT_SHAPES] = 0;
	}

	max_response_val = max_response_val_ + CLIPPED_IMG_WIDTH;

	int_tl = integral - radius / 2 * PROC_IMG_WIDTH / 4 - radius / 4;
	int_br = integral + radius / 2 * PROC_IMG_WIDTH / 4 + radius / 4;
	int_tr = integral - radius / 2 * PROC_IMG_WIDTH / 4 + radius / 4;
	int_bl = integral + radius / 2 * PROC_IMG_WIDTH / 4 - radius / 4;
}

#if defined(GUI)
bool Image::getPt(int x, int y)
{
	const unsigned char * const src = (unsigned char *) binarized->imageData;
	return src[y * PROC_IMG_WIDTH + x];
}
#endif

#define U ((2 * radius) * (2 * radius) * 1.03 / 2 / 4)
#define U2 ((2 * radius) * (2 * radius) * 0.85 / 2 / 4)

bool Image::instantGetPt(int x, int y)
{
	const int u = y / 2 * PROC_IMG_WIDTH / 4;
	const int v = x / 4;
	const int w = u + v;
	return U * (unsigned char) frame[y * INPUT_IMG_WIDTH + x] > int_tl[w] + int_br[w] - int_tr[w] - int_bl[w];
}

bool Image::instantGetPt2(int x, int y)
{
	const int u = y / 2 * PROC_IMG_WIDTH / 4;
	const int v = x / 4;
	const int w = u + v;
	return U2 * (unsigned char) frame[y * INPUT_IMG_WIDTH + x] < int_tl[w] + int_br[w] - int_tr[w] - int_bl[w];
}

#if defined(GUI)
void Image::setPt(int x, int y)
{
	unsigned char * const src = (unsigned char*) this->src->imageData;
	if(0 < x && x < PROC_IMG_WIDTH && 0 < y && y < PROC_IMG_HEIGHT)
		src[y * PROC_IMG_WIDTH + x] ^= 150;
}
#endif

void Image::integral_image(const unsigned char *src)
{
	int j, k;
	const unsigned char *src_ = src + bpi * INPUT_IMG_WIDTH * (INPUT_IMG_HEIGHT - height_margin);

	frame = (const unsigned short *) src + height_margin * INPUT_IMG_WIDTH + width_margin;

	src += bpi * (height_margin * INPUT_IMG_WIDTH + width_margin);

	for (j = 0, k = 0; j < PROC_IMG_WIDTH / 4; src += bpi * 4) {
		k += *src;
		integral[j++] = k;
	}
	src += bpi * (INPUT_IMG_WIDTH + 2 * width_margin);

	unsigned int *integral_ = integral;
	for (; src < src_;) {
		for (unsigned int *integral__ = integral_ + PROC_IMG_WIDTH / 4; integral_ < integral__;
		        integral_++, src += bpi * 4) {
			k += *src;
			integral_[PROC_IMG_WIDTH / 4] = *integral_ + k;
		}
		src += bpi * (INPUT_IMG_WIDTH + 2 * width_margin);
	}

#if defined(GUI)
	unsigned char * const dst = (unsigned char *) binarized->imageData;
	for (int i = radius + 1; i < PROC_IMG_HEIGHT - radius - 1; i++)
		for (int j = radius + 1; j < PROC_IMG_WIDTH - radius - 1; j++)
			dst[i * PROC_IMG_WIDTH + j] = 255 * instantGetPt(j, i);
#endif
}

static int rnd = 0;

void Image::initScan()
{
#if defined(LIVE)
	rnd++;
	rnd %= scanJumpSize;
#endif

	y0 = radius + rnd;
	incY = tan(theta);
	slashBlackWidth = 0;
}

bool Image::isGoodSlashTrain(int num_slash_to_find, int* slashBin, int slashBinSize)
{
	float f, g;

	trainLength = 0;
	for (int i = 1; i < slashBinSize; i++)
		trainLength += slashBin[i];

	f = (float) trainLength / (float) (num_slash_to_find - 1);
	if (slashPeriod != 0 && (f / slashPeriod > 1.2f || f / slashPeriod < .8f))
		return false;

	for (int i = 1; i < slashBinSize; i += 2) {
		g = slashBin[i] + slashBin[i + 1];
		if (g < 0.7f * f || g > 1.4f * f)
			return false;
	}

	return true;
}

void Image::calcAccurateOrientation()
{
//	int *ptx, *pty, ptl;
	int ptl = 0;
	int ptx_, pty_;
//	int b1 = 0, b2 = 0;

	finderEndX = trainEndX;
	finderEndY = trainEndY;

	/* get black points on scan line */
	{
		float x = trainEndX, y = trainEndY;

		if (blackOnWhite) {
			bool g_ = !instantGetPt(x, y); /* previous value */

			for (int i = 0; i < trainLength; i++, x-- /*-= incX*/, y -= incY) {
				/* skip same-color-as-previous pixels */
				const bool g = instantGetPt(x, y);
				if (g == g_)
					continue;
				g_ = g;

				if (!g) {
					int inc = 1, t = 0;
					ptx_ = x;
					pty_ = y;

					/* track each point downward as much as possible, until it becomes white */
					while (ptx_ < PROC_IMG_WIDTH - 1 - radius && pty_ < PROC_IMG_HEIGHT - 1 - radius) {
						if (!instantGetPt(ptx_, ++pty_)) {
							t = 0;
							continue;
						}
						if (!instantGetPt(ptx_ + inc, --pty_)) {
							ptx_ += inc;
							continue;
						}
						inc *= -1;
						t++;
						if (t == 3)
							break;
					}

					ptx[ptl] = ptx_;
					pty[ptl++] = pty_;
				}
			}

		} else { /* white on black */
			bool g_ = !instantGetPt2(x, y); // previous value

			for (int i = 0; i < trainLength; i++, x-- /*-= incX*/, y -= incY) {
				/* skip same-color-as-previous pixels */
				const bool g = instantGetPt2(x, y);
				if (g == g_)
					continue;
				g_ = g;

				if (!g) {
					int inc = 1, t = 0;
					ptx_ = x;
					pty_ = y;

					/* track each point downward as much as possible, until it becomes white */
					while (ptx_ > radius && ptx_ < PROC_IMG_WIDTH - 1 - radius && pty_ < PROC_IMG_HEIGHT - 1 - radius) {
						if (!instantGetPt2(ptx_, ++pty_)) {
							t = 0;
							continue;
						}
						if (!instantGetPt2(ptx_ + inc, --pty_)) {
							ptx_ += inc;
							continue;
						}
						inc *= -1;
						t++;
						if (t == 3)
							break;
					}

					ptx[ptl] = ptx_;
					pty[ptl++] = pty_;
				}
			}
		}
	}

	if (ptl < p)
		return;

	/* find robust bisquare regression line */
	gsl_matrix * const X = X_[ptl];
	gsl_vector * const y = y_[ptl];
	gsl_multifit_robust_workspace * const work = work_[ptl];

	for (int s = 0; s < ptl; s++) {
		gsl_vector_set(y, s, pty[s]);
		gsl_matrix_set(X, s, 1, ptx[s]);
	}

	/* perform bisquare robust fit */
	gsl_multifit_robust(X, y, c, cov, work);

	/* update incX and incY and theta */
	incY = trainSlope = gsl_vector_get(c, 1); // slope
	bottomLineIntercept = gsl_vector_get(c, 0);
	y0 = bottomLineIntercept + scanJumpSize / 2; // update scan line intercept
}

bool Image::findTopBoundOfFinderTrain()
{
	int ptl = 0;
	int ptx_, pty_;

	/* get black points on scan line */
	if (blackOnWhite) {
		float x = finderEndX, y = finderEndY;
		bool g_ = !instantGetPt(x, y); // previous value

		for (int i = finderTrainLength; i; i--, x-- /*-= incX*/, y -= incY) {
			/* skip same-color-as-previous pixels */
			const bool g = instantGetPt(x, y);
			if (g == g_)
				continue;
			g_ = g;

			if (!g) {
				int inc = 1, t = 0;

				ptx_ = x;
				pty_ = y;

				/* track each point upward as much as possible, until it becomes white */
				while (ptx_ > radius && ptx_ < PROC_IMG_WIDTH - 1 - radius && pty_ > radius) {
					if (!instantGetPt(ptx_, --pty_)) {
						t = 0;
						continue;
					}
					if (!instantGetPt(ptx_ + inc, ++pty_)) {
						ptx_ += inc;
						continue;
					}
					inc *= -1;
					t++;
					if (t == 3)
						break;
				}

				ptx[ptl] = ptx_;
				pty[ptl++] = pty_;
//				std::cout << ptx_ << "," << pty_ << "  ";
			}
		}
//		std::cout << std::endl;

	} else { // black on white
		float x = finderEndX, y = finderEndY;
		bool g_ = !instantGetPt2(x, y); // previous value

		for (int i = finderTrainLength; i; i--, x-- /*-= incX*/, y -= incY) {
			/* skip same-color-as-previous pixels */
			const bool g = instantGetPt2(x, y);
			if (g == g_)
				continue;
			g_ = g;

			if (!g) {
				int inc = 1, t = 0;

				ptx_ = x;
				pty_ = y;

				/* track each point upward as much as possible, until it becomes white */
				while (ptx_ > radius && ptx_ < PROC_IMG_WIDTH - 1 - radius && pty_ > radius) {
					if (!instantGetPt2(ptx_, --pty_)) {
						t = 0;
						continue;
					}
					if (!instantGetPt2(ptx_ + inc, ++pty_)) {
						ptx_ += inc;
						continue;
					}
					inc *= -1;
					t++;
					if (t == 3)
						break;
				}

				ptx[ptl] = ptx_;
				pty[ptl++] = pty_;
//				std::cout << ptx_ << "," << pty_ << "  ";
			}
		}
//		std::cout << std::endl;
	}

	if (ptl < p)
		return false;

	/* find robust bisquare regression line */
	gsl_matrix * const X = X_[ptl];
	gsl_vector * const y = y_[ptl];
	gsl_multifit_robust_workspace * const work = work_[ptl];

	for (int s = 0; s < ptl; s++) {
		gsl_vector_set(y, s, pty[s]);
		gsl_matrix_set(X, s, 1, ptx[s]);
	}

	/* perform bisquare robust fit */
	gsl_multifit_robust(X, y, c, cov, work);

	const float intercept = gsl_vector_get(c, 0);
	const float slope = gsl_vector_get(c, 1);
	topLineIntercept = intercept + (slope - trainSlope) * finderEndX;
	trainHeight = bottomLineIntercept - topLineIntercept;

	return true;
}

bool Image::calcBoundingLines()
{
//	static int ptx[PROC_IMG_WIDTH], pty[PROC_IMG_WIDTH]; // top- and right-most black points on slashes
	int ptl = 0;
	int ptx_, pty_;

	/* get black points on scan line */
	if (blackOnWhite) {
		float x = trainEndX - trainLength /** incX*/, y = trainEndY - trainLength * incY;
		bool g_ = !instantGetPt(x, y); // previous value

		for (int i = 6 * trainLength - 1; i >= 0; i--, x++, y += incY) {
			/* skip same-color-as-previous pixels */
			const bool g = instantGetPt(x, y);
			if (g == g_)
				continue;
			g_ = g;

			if (!g) {
				int inc = 1, t = 0;

				ptx_ = x;
				pty_ = y;

				/* track each point upward as much as possible, until it becomes white */
				while (ptx_ > radius && ptx_ < PROC_IMG_WIDTH - 1 - radius && pty_ > radius) {
					if (!instantGetPt(ptx_, --pty_)) {
						t = 0;
						continue;
					}
					if (!instantGetPt(ptx_ + inc, ++pty_)) {
						ptx_ += inc;
						continue;
					}
					inc *= -1;
					t++;
					if (t == 3)
						break;
				}

				ptx[ptl] = ptx_;
				pty[ptl++] = pty_;
//				std::cout << ptx_ << "," << pty_ << " ";
			}
		}
//		std::cout << '\n';

	} else { /* white text on black background */
		float x = trainEndX - trainLength /** incX*/, y = trainEndY - trainLength * incY;
		bool g_ = !instantGetPt2(x, y); // previous value

		for (int i = 6 * trainLength - 1; i >= 0; i--, x++, y += incY) {
			/* skip same-color-as-previous pixels */
			const bool g = instantGetPt2(x, y);
			if (g == g_)
				continue;
			g_ = g;

			if (!g) {
				int inc = 1, t = 0;

				ptx_ = x;
				pty_ = y;

				/* track each point upward as much as possible, until it becomes white */
				while (ptx_ > radius && ptx_ < PROC_IMG_WIDTH - 1 - radius && pty_ > radius) {
					if (!instantGetPt2(ptx_, --pty_)) {
						t = 0;
						continue;
					}
					if (!instantGetPt2(ptx_ + inc, ++pty_)) {
						ptx_ += inc;
						continue;
					}
					inc *= -1;
					t++;
					if (t == 3)
						break;
				}

				ptx[ptl] = ptx_;
				pty[ptl++] = pty_;
//				std::cout << ptx_ << "," << pty_ << " ";
			}
		}
//		std::cout << '\n';
	}

	if (ptl < p)
		return false;

	/* find robust bisquare regression line above code train */
	gsl_matrix * const X = X_[ptl];
	gsl_vector * const y = y_[ptl];
	gsl_multifit_robust_workspace * const work = work_[ptl];

	for (int s = 0; s < ptl; s++) {
		gsl_vector_set(y, s, pty[s]);
		gsl_matrix_set(X, s, 1, ptx[s]);
	}

	/* perform bisquare robust fit */
	gsl_multifit_robust(X, y, c, cov, work);

	incY = trainSlope = gsl_vector_get(c, 1); // slope

	const float intercept = gsl_vector_get(c, 0);
	rowHeight = intercept - topLineIntercept;
	topLineIntercept = intercept;

	bottomLineIntercept = topLineIntercept + trainHeight;
	y0 = bottomLineIntercept + scanJumpSize / 2;

	return true;
}

void Image::clip(int height) // rotate and clip
{
	cvGetAffineTransform(dstAnchors, srcAnchors, affineMat);
	int i, j;
	int *dst = clipped;
	const float a = *affineMat->data.fl, b = *(affineMat->data.fl + 1), c = *(affineMat->data.fl + 2);
	const float d = *(affineMat->data.fl + 3), e = *(affineMat->data.fl + 4), f = *(affineMat->data.fl + 5);
	float eif, bic;

	if (blackOnWhite) {
		for (i = 0; i < height; i++) {
			eif = e * i + f;
			bic = b * i + c;
			for (j = 0; j < CLIPPED_IMG_WIDTH; j++, dst++)
				*dst = 2 * instantGetPt(a * j + bic, d * j + eif) - 1;
		}

	} else {
		for (i = 0; i < height; i++) {
			eif = e * i + f;
			bic = b * i + c;
			for (j = 0; j < CLIPPED_IMG_WIDTH; j++, dst++)
				*dst = 2 * instantGetPt2(a * j + bic, d * j + eif) - 1;
		}
	}
}

void Image::filter2d()
{
	const int length = CLIPPED_IMG_MAX_HEIGHT - CLIPPED_IMG_WIDTH + 1;
	const int L = CLIPPED_IMG_WIDTH * CLIPPED_IMG_WIDTH;
	int i, j, k, a;
	int *sym, *response, response_;

	for (k = 0; k < length; k++) {
		response = this->response[k];

		const int *x = clipped + k * CLIPPED_IMG_WIDTH;
		for (j = 0; j < (int) N; j++) {
			for (a = 0, response[j] = INT_MIN; (sym = this->sym[j][a]); a++) {
				for (i = 0, response_ = 0; i < L; i++) {
					response_ += x[i] * sym[i];
				}
				if (response_ > response[j])
					response[j] = response_;
			}
		}

		max_response_idx[k] = 0;
		max_response_val[k] = response[0];
		for (j = 1; j < (int) N; j++) {
			if (response[j] > max_response_val[k]) {
				max_response_idx[k] = j;
				max_response_val[k] = response[j];
			}
		}
	}

	/* extend first value backward */
	for (int *val = max_response_val_; val < max_response_val; val++)
		*val = max_response_val[0];
}

int Image::nextMax(unsigned int *sym_idx)
{
	const int cursor_max = CLIPPED_IMG_MAX_HEIGHT - CLIPPED_IMG_WIDTH + 1 - maximWinSize;
	int i;

	while (++cursor < cursor_max) {
		for (i = 1; i < maximWinSize; i++) {
			if (max_response_val[cursor] <= max_response_val[cursor + i]
			        || max_response_val[cursor] < max_response_val[cursor - i])
				break;
		}
		if (i == maximWinSize) {
			*sym_idx = max_response_idx[cursor];
//			switch (*sym_idx)
//			{
//			case 0: /* / */
//			case 1: /* \ */
			if (max_response_val[cursor] < 0.6 * min_hill_height)
				continue;
			if (min_hill_height && (*sym_idx == 0 /* / */|| *sym_idx == 1 /* \ */))
				min_hill_height = (2 * min_hill_height + max_response_val[cursor]) / 3;
//			break;
//			case 2: /* T */
//				if (max_response_val[cursor] < 0.4 * min_hill_height)
//					continue;
//				break;
//			}
			return cursor;
		}
	}

	return 0;
}

bool Image::find_next_slash_train(int num_slash_to_find, int scan_jump_size, int certainty)
{
	int k;

	slashBinSize = 2 * num_slash_to_find - 1;
	const float maxY = certainty == codeFinderCertainty ? 4 * PROC_IMG_HEIGHT / 5 : PROC_IMG_HEIGHT - 2 * radius;

	for (y0 += scan_jump_size / 3; y0 < PROC_IMG_HEIGHT - 3 * radius; y0 += scan_jump_size) {
		Y = y0;
		X = radius + 1;

		if (blackOnWhite || certainty == codeFinderCertainty) {
			blackOnWhite = true;
			if (certainty == codeFinderCertainty)
				slashPeriod = 0;

			x = X;
			y = Y;

			reset_bin: ;
			ibin = 0;
			for (k = 0; k < slashBinSize; k++)
				slashBin[k] = 0;

			while (++x < PROC_IMG_WIDTH - 2 * radius && radius <= (y += incY) && y < maxY) {
				if (!instantGetPt(x, y)) {
					if (ibin % 2) {
						if (slashBin[ibin] < 2) // remove noise
							slashBin[ibin--] = 0;
						else if (slashBin[ibin] > 30) // white slash is too thick
							goto reset_bin;
						else
							ibin++; // next bin for new black sequence
					}
					slashBin[ibin]++;
					goto next_pixel;
				}

				/* retry in current line until we pass first slash */
				if (!slashBin[0])
					goto next_pixel;

				if (!(ibin % 2)) {
					if (slashBin[ibin] < 2) // remove noise
						slashBin[ibin--] = 0;
					else if (slashBin[ibin] > 25) // black slash is too thick
						goto reset_bin;
					else if (++ibin == slashBinSize) {
						if (isGoodSlashTrain(num_slash_to_find, slashBin, slashBinSize)) {
							trainEndX = x;
							trainEndY = y;

							int train_length;
							if (certainty == codeFinderCertainty) {
								finderTrainLength = trainLength;
								slashPeriod = (float) (finderTrainLength - slashBlackWidth)
								        / (float) (codeFinderSlashCount - 1);
								train_length = 1.3 * finderTrainLength;
							} else
								train_length = 1.6 * finderTrainLength * rowFinderSlashCount / codeFinderSlashCount;

							float e = 0;
							const float o = certainty == codeFinderCertainty ? 1.5 : 2.5;
							for (float t = 1; t < certainty; t++) {
								if (!verify_slash_train(num_slash_to_find, train_length, t - o))
									e++;
							}
							if (e / certainty > 0.4)
								goto bad_slash_train;

							slashBlackWidth = 0;
							for (int i = 2; i <= slashBinSize; i += 2) {
								slashBlackWidth += slashBin[i];
							}
							slashBlackWidth /= num_slash_to_find - 1;

							return true;
						}

						bad_slash_train: ;

						ibin -= 2;
						for (k = 0; k < ibin; k++)
							slashBin[k] = slashBin[k + 2];
						slashBin[slashBinSize - 2] = slashBin[slashBinSize - 1] = 0;
					}
				}

				slashBin[ibin]++;

				next_pixel: ;
#if defined(DEBUG_VIEW) && defined(GUI)
//				if (certainty != codeFinderCertainty)
//					setPt(x, y);
#endif
			}
		}

		if (!blackOnWhite || certainty == codeFinderCertainty) {
			blackOnWhite = false;
			if (certainty == codeFinderCertainty)
				slashPeriod = 0;

			x = X;
			y = Y;

			reset_bin2: ;
			ibin = 0;
			for (k = 0; k < slashBinSize; k++)
				slashBin[k] = 0;

			while (++x < PROC_IMG_WIDTH && radius <= (y += incY) && y < PROC_IMG_HEIGHT) {
				if (!instantGetPt2(x, y)) {
					if (ibin % 2) {
						if (slashBin[ibin] > 30) // white slash is too thick
							goto reset_bin2;
						ibin++; // next bin for new black sequence
					}
					slashBin[ibin]++;
					goto next_pixel2;
				}

				/* retry in current line until we pass first slash */
				if (!slashBin[0])
					goto next_pixel2;

				if (!(ibin % 2)) {
					if (slashBin[ibin] > 25) // black slash is too thick
						goto reset_bin2;

					if (++ibin == slashBinSize) {
						if (isGoodSlashTrain(num_slash_to_find, slashBin, slashBinSize)) {
							int train_length;
							trainEndX = x;
							trainEndY = y;

							/* TODO check this in the case of white on black text */
							if (certainty == codeFinderCertainty) {
								slashBlackWidth = 0;
								for (int i = 2; i <= slashBinSize; i += 2) {
									slashBlackWidth += slashBin[i];
								}
								slashBlackWidth /= num_slash_to_find - 1;
								finderTrainLength = trainLength;
								slashPeriod = (float) (finderTrainLength - slashBlackWidth)
								        / (float) (codeFinderSlashCount - 1);
								train_length = 1.3 * finderTrainLength;

							} else
								train_length = 1.6 * finderTrainLength * rowFinderSlashCount / codeFinderSlashCount;

							float e = 0;
							const float o = certainty == codeFinderCertainty ? 1.5 : 2.5;
							for (float t = 1; t < certainty; t++) {
								if (!verify_slash_train(num_slash_to_find, train_length, t - o))
									e++;
							}
							if (e / certainty > 0.4)
								goto bad_slash_train2;

							return true;
						}

						bad_slash_train2: ;
						ibin -= 2;
						for (k = 0; k < ibin; k++)
							slashBin[k] = slashBin[k + 2];
						slashBin[slashBinSize - 2] = slashBin[slashBinSize - 1] = 0;
					}
				}

				slashBin[ibin]++;

				next_pixel2: ;
#if defined(DEBUG_VIEW) && defined(GUI)
//				if (certainty == codeFinderCertainty)
//					setPt(x, y);
#endif
			}
		}
	}

	return false;
}

bool Image::verify_slash_train(int num_slash_to_find, int train_length, float r)
{
	int slashBinSize_, ibin_ = 0;
	int k;

	slashBinSize_ = 2 * num_slash_to_find;

	float y = trainEndY + r * scanJumpSize / 5.0;
	float x = trainEndX;

	for (k = 0; k < slashBinSize_; k++)
		slashBin_[k] = 0;

	while (train_length-- && --x > radius && radius <= (y -= incY) && y < PROC_IMG_HEIGHT) {

		if (!(blackOnWhite ? instantGetPt(x, y) : instantGetPt2(x, y)) || !train_length) {

			if (ibin_ % 2) {
				if (slashBin_[ibin_] < 2) // remove noise
					slashBin_[ibin_--] = 0;
				else if (++ibin_ == slashBinSize_) {
					if (slashBin_[ibin_ - 1] > 1.35 * slashPeriod)
						if (isGoodSlashTrain(num_slash_to_find, slashBin_, slashBinSize_ - 1))
							return true;
					ibin_ -= 2;
					for (k = 0; k < ibin_; k++)
						slashBin_[k] = slashBin_[k + 2];
					slashBin_[slashBinSize_ - 2] = slashBin_[slashBinSize_ - 1] = 0;
				}
			}
			slashBin_[ibin_]++;
			goto next_pixel_;
		}

		/* retry in current line until we pass first slash */
		if (!slashBin_[0])
			goto next_pixel_;

		if (!(ibin_ % 2)) {
			if (slashBin_[ibin_] < 2) // remove noise
				slashBin_[ibin_--] = 0;
			else
				ibin_++;
		}
		slashBin_[ibin_]++;

		next_pixel_: ;
#if defined(DEBUG_VIEW) && defined(GUI)
//		if (num_slash_to_find == rowFinderSlashCount)
//			setPt(x, y);
#endif
	}

	return false;
}

int read_int(FILE *fp)
{
	std::string str;
	char c = 0, d;

	while (!feof(fp)) {
		d = c;
		c = (char) fgetc(fp);
		if (c >= '0' && c <= '9') {
			if (d == '-')
				str += ('-');
			str += c;
			break;
		}
	}

	while (!feof(fp)) {
		d = c;
		c = (char) fgetc(fp);
		if ((c >= '0' && c <= '9'))
			str += c;
		else
			break;
	}

	return atoi(str.c_str());
}

#endif // SCAN
