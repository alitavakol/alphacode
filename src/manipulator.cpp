#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define PROC_IMG_WIDTH INPUT_IMG_WIDTH
#define PROC_IMG_HEIGHT INPUT_IMG_HEIGHT

#if defined(SCAN) && !defined(LIVE)
#define DIRECTORY "D:\\alpha\\Experiments\\12"
#define IMAGE_PATH DIRECTORY"/frame11.yuv" // capture a single frame from image file
//#define VIDEO_PATH DIRECTORY"/capture1.yuv" // capture from video file
#endif
#if (defined(VIDEO_PATH) && defined(LIVE)) || (defined(VIDEO_PATH) && defined(IMAGE_PATH)) || (defined(IMAGE_PATH) && defined(LIVE))
#error "cannot specify multiple macros at the same time"
#endif

#if defined(GENERATE)
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <math.h>
#endif
#include <stdio.h>
#if defined(SCAN)
#include <gsl/gsl_multifit.h>
#include <boost/thread.hpp>
#endif
#if defined(LIVE)
#include <sys/wait.h>
#endif
#if defined(GUI)
#include <opencv/highgui.h>
#endif
#if defined(DURATION)
#include <boost/chrono.hpp>
#endif
#include "alphacode.h"
#if defined(SCAN)
#include "Image.h"
#include "sqlite3.h"
#endif
#if defined(IMAGE_PATH) || defined(VIDEO_PATH)
#include <boost/algorithm/string.hpp>
#endif
#include <iostream>

#if defined(LIVE)
extern int main_(int argc, char **argv);
#endif
#if defined(VIDEO_PATH) || defined(LIVE)
#if !defined(WIN32)
extern int exit_loop;
#else
int exit_loop = 0;
#endif
#endif

#if defined(SCAN)
Image *ip;
#endif
AlphaCode *alphaCode;

#if defined(SCAN)
sqlite3 *db;
char *sql_error = 0;
std::string punch_table_name;
static int sqlite_callback(void *data, int argc, char **argv, char **azColName)
{
	int *count = (int*) data;
	*count = atoi(argv[0]);
	return 0;
}
#endif

#if defined(LIVE)
boost::chrono::time_point<boost::chrono::system_clock> lastDetectTime;
bool detected = false;
const float detect_margin_time = 1.5;
#endif

#if defined(SCAN)
int frameCounter;
boost::chrono::time_point<boost::chrono::system_clock> start, end;
#endif

#if defined(GUI)
IplImage *view;
CvFont font1, font2;
char str[100];
#endif

#if defined(LIVE) && !defined(GUI)
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"
#else
#define KNRM ""
#define KRED ""
#define KGRN ""
#define KYEL ""
#define KBLU ""
#define KMAG ""
#define KCYN ""
#define KWHT ""
#define RESET ""
#endif

struct Params {
	const static size_t N = TICKET_SYMBOL_COUNT;
	const static size_t C = TICKET_LINE_LENGTH;
	const static size_t R = TICKET_CHECKSUM_LENGTH;
	const static size_t D = TICKET_CORRELATION;
	const static size_t L = TICKET_LINES;
};
Params params;

const uint32_t event_id = alphaCode->initial_seed;

#if defined(LIVE)
bool fork_process = false;
#endif

#if defined(LIVE)
void blink(int status);
#endif

#define MAX_C 31

/*
 for configuration code:
 (N,C,R,D,L) = (4,15,9,5,3)

 value	range		bits
 -----	-----		----
 N		0:15		4
 C		0:31		5
 R		0:31		5
 D		0:31		5
 L		0:7			3
 E		0:16383		14

 encode:
 code = (((((((((E << 3) | L) << 5) | D) << 5) | R) << 5) | C) << 4) | N;

 decode:
 N = code & 0xf;
 code >>= 4;
 C = code & 0x1f;
 code >>= 5;
 R = code & 0x1f;
 code >>= 5;
 D = code & 0x1f;
 code >>= 5;
 L = code & 0x7;
 code >>= 3;
 E = code & 0x3fff;
 */

unsigned int sym_[rowFinderSlashCount + MAX_C], *sym = sym_ + rowFinderSlashCount; // detected symbols in current row

void gsl_error_handler(const char * reason, const char * file, int line, int gsl_errno)
{
}

#if defined(SCAN)
bool create_punch_table(uint32_t event_id)
{
	if (!db)
		return false;

	{
		std::stringstream ss;
		ss << "punched_tickets_" << event_id;
		punch_table_name = ss.str().c_str();
	}

	std::stringstream ss;
	ss << "create table " << punch_table_name << " (id integer primary key autoincrement, ticket_number int not null);";

	if (sqlite3_exec(db, ss.str().c_str(), sqlite_callback, 0, &sql_error) != SQLITE_OK) {
		std::cerr << KRED << sql_error << RESET"\n";
		sqlite3_free(sql_error);
		return false;
	}

	return true;
}

int verify_ticket(int number)
{
	if (!db)
		return 0;

	std::stringstream ss;
	ss << "select count(*) from " << punch_table_name << " where ticket_number = " << number << ";";

	int count;

	if (sqlite3_exec(db, ss.str().c_str(), sqlite_callback, &count, &sql_error) != SQLITE_OK) {
		std::cerr << KRED << sql_error << RESET"\n";
		sqlite3_free(sql_error);
		return -1;
	}

	return count;
}

int add_ticket(int number)
{
	if (!db)
		return 0;

	std::stringstream ss;
	ss << "insert into " << punch_table_name << " (ticket_number) values (" << number << ");";

	if (sqlite3_exec(db, ss.str().c_str(), sqlite_callback, 0, &sql_error) != SQLITE_OK) {
		std::cerr << KRED << sql_error << RESET"\n";
		sqlite3_free(sql_error);
		return -1;
	}

	return 0;
}
#endif

#if defined(VIDEO_PATH)
FILE *fp;
#endif

#if defined(SCAN)
void init()
{
	ip = new Image(params.N);

#if defined(GUI)
	view = ip->src; // cvCreateImage(cvSize(PROC_IMG_WIDTH, PROC_IMG_HEIGHT), IPL_DEPTH_8U, 1);
	cvInitFont(&font1, CV_FONT_HERSHEY_SIMPLEX, 0.5, 0.5, 0, 2);
	cvInitFont(&font2, CV_FONT_HERSHEY_SIMPLEX, 0.3, 0.3, 0, 1);
#endif

	frameCounter = 0;
	start = boost::chrono::system_clock::now();
}

void process(const char *frame)
{
	frameCounter++;

#if defined(LIVE)
	if (detected) {
		float delay = ((boost::chrono::system_clock::now() - lastDetectTime).count()
				* (double) boost::chrono::system_clock::period::num / boost::chrono::system_clock::period::den);
		if (delay < detect_margin_time)
		return;
		detected = false;
	}
#endif

#if defined(DURATION)
	boost::chrono::time_point<boost::chrono::system_clock> binarizeStart = boost::chrono::system_clock::now();
#endif

#if defined(GUI)
	{
		const char *frame_ = frame + bpi * (height_margin * INPUT_IMG_WIDTH + width_margin);
		char *image = ip->src->imageData;
		for (int i = 0; i < PROC_IMG_HEIGHT; i++, frame_ += bpi * INPUT_IMG_WIDTH)
			for (int j = 0; j < PROC_IMG_WIDTH; j++)
				*image++ = frame_[bpi * j];
	}
#endif

//	ip->binarize((unsigned char *) frame);
	ip->integral_image((const unsigned char *) frame);

#if defined(DURATION)
	boost::chrono::time_point<boost::chrono::system_clock> binarizeEnd = boost::chrono::system_clock::now();
#if defined(GUI)
	sprintf(str, "binarize %.0f us",
	        ((binarizeEnd - binarizeStart).count() * (double) boost::chrono::system_clock::period::num
	                / boost::chrono::system_clock::period::den) * 1e6);
#if defined(GUI)
	cvPutText(ip->binarized, str, cvPoint(10, 20), &font1, cvScalar(155, 155, 155, 0));
#endif
#else
	std::cout << "binarize " << (int)( ((binarizeEnd - binarizeStart).count() * (double) boost::chrono::system_clock::period::num
					/ boost::chrono::system_clock::period::den) * 1e6) << " us\n";
#endif
#endif

#if defined(DURATION)
	boost::chrono::time_point<boost::chrono::system_clock> scanStart = boost::chrono::system_clock::now();
#endif

#if defined (USE_HOUGH)
#if defined(DURATION)
	boost::chrono::time_point<boost::chrono::system_clock> houghStart = boost::chrono::system_clock::now();
#endif

	const bool orientationFound = ip->findOrientation();

#if defined(DURATION)
	boost::chrono::time_point<boost::chrono::system_clock> houghEnd = boost::chrono::system_clock::now();
#if defined(GUI)
	sprintf(str, "orientation %.0f us",
			((houghEnd - houghStart).count() * (double) boost::chrono::system_clock::period::num
					/ boost::chrono::system_clock::period::den) * 1e6);
	cvPutText(view, str, cvPoint(10, 20), &font1, cvScalar(155, 155, 155, 0));
#else
	std::cout << "orientation " << (int)( ((houghEnd - houghStart).count() * (double) boost::chrono::system_clock::period::num
					/ boost::chrono::system_clock::period::den) * 1e6) << " us\n";
#endif
#endif

	if (orientationFound)
	{
#if defined(GUI)
		// show orientation
		double a = cos(ip->theta);
		double b = -sin(ip->theta);
		double x0 = a * ip->rho;
		double y0 = -b * ip->rho;
		static CvPoint pt1, pt2;
		pt1.x = cvRound(x0 + 1000 * b);
		pt1.y = cvRound(y0 + 1000 * a);
		pt2.x = cvRound(x0 - 1000 * b);
		pt2.y = cvRound(y0 - 1000 * a);
		cvLine(view, pt1, pt2, cvScalar(255, 0, 0, 0), 1, 0);
#endif
#else // not using hough
	bool ignore_other_orientations = false;
	const int N = 2;
#if !defined(IMAGE_PATH)
	for (int o = 0; o < N && !ignore_other_orientations; o++) {
#else
//		int o = 2;
//		{
	for (int o = 0; o < N /*&& !ignore_other_orientations*/; o++) {
#endif
		ip->theta = (rotation_span * o / N - rotation_span * (N - 1) / 2 / N) * 0.017453293f;
#endif

		ip->initScan();

		while (!ignore_other_orientations
		        && ip->find_next_slash_train(codeFinderSlashCount, scanJumpSize, codeFinderCertainty)) {
			const int finderTrainLength = ip->finderTrainLength;

			if (finderTrainLength < PROC_IMG_WIDTH / 15)
				continue;

			ip->calcAccurateOrientation();
#if defined(DEBUG_VIEW) && defined(GUI)
			/* draw line below finder slash train */
			for (int x = 150; x < PROC_IMG_WIDTH - 200; x++) {
				const float y = ip->bottomLineIntercept + ip->trainSlope * x;
				if (y < 0 || y >= PROC_IMG_HEIGHT)
					continue;
				ip->setPt(x, y);
			}
#endif

			float angle = atan(ip->trainSlope);

#if defined(DURATION)
			boost::chrono::time_point<boost::chrono::system_clock> getRowsStart = boost::chrono::system_clock::now();
#endif

			CvPoint2D32f * const srcAnchors = ip->srcAnchors;
			// value of these anchor points are chosen in such a way that
			// slash pulses in clipped, take a 45 degree slope
			srcAnchors[0].x = ip->trainEndX - finderTrainLength;
			srcAnchors[1].x = srcAnchors[0].x + finderTrainLength;

			uint64_t detected_code = 0;

			for (size_t row = 0, tmp = 0; row < params.L && !detected_code; row++, tmp++) {
				const int y0 = ip->y0;

				next_code_train: ;
				if (!ip->find_next_slash_train(rowFinderSlashCount, scanJumpSize / 2, rowFinderCertainty))
					goto next_finder_train;

				if (ip->y0 - y0 > 6 * scanJumpSize)
					goto next_finder_train;
//				else if (ip->y0 - y0 > 4 * scanJumpSize) {
//					if (++row >= SmsCode::L)
//						goto next_finder_train;
//				}

				if (!tmp) {
					if (!ip->findTopBoundOfFinderTrain())
						goto next_finder_train;
#if defined(DEBUG_VIEW) && defined(GUI)
					/* draw line above finder train */
					for (int x = 150; x < PROC_IMG_WIDTH - 200; x++) {
						const float y = ip->topLineIntercept + ip->trainSlope * x;
						if (y < 0 || y >= PROC_IMG_HEIGHT)
							continue;
						ip->setPt(x, y);
					}
#endif
				}

				if (!ip->calcBoundingLines())
					goto next_finder_train;

				if (ip->y0 >= PROC_IMG_HEIGHT)
					goto next_finder_train;

				const float trainSlope = ip->trainSlope;
				const float angle_ = atan(trainSlope);
				const float d_angle = std::abs(angle - angle_);
				const int slope_max_deviation = 4; // maximum difference between row orientation angles [degrees]
				if (d_angle > slope_max_deviation * M_PI / 180) // fail if slope changes drastically relative to previous row
					goto next_finder_train;
				angle = angle_;

#if defined(DEBUG_VIEW) && defined(GUI)
				/* draw code row bounding lines */
				for (int x = 150; x < PROC_IMG_WIDTH - 200; x++) {
					const float y = ip->topLineIntercept + trainSlope * x;
					if (y < 0 || y >= PROC_IMG_HEIGHT)
						continue;
					ip->setPt(x, y);
					const float y2 = ip->bottomLineIntercept + trainSlope * x;
					if (y2 < 0 || y2 >= PROC_IMG_HEIGHT)
						continue;
					ip->setPt(x, y2);
				}
#endif

				if (alphaCode->correct_not_expired(row))
					continue;

				ignore_other_orientations = true;

#if defined(DURATION)
				boost::chrono::time_point<boost::chrono::system_clock> clipStart = boost::chrono::system_clock::now();
#endif
				/* clip and rotate code train */
				srcAnchors[0].x = ip->trainEndX - 2 * ip->trainLength;
				srcAnchors[1].x = srcAnchors[0].x + finderTrainLength;
				srcAnchors[2].x = srcAnchors[0].x
				        + trainSlope / (1 + trainSlope * trainSlope) * (ip->topLineIntercept - ip->bottomLineIntercept);
				srcAnchors[0].y = ip->trainSlope * srcAnchors[0].x + ip->topLineIntercept;
				srcAnchors[1].y = ip->trainSlope * srcAnchors[1].x + ip->topLineIntercept;
				srcAnchors[2].y = ip->trainSlope * srcAnchors[2].x + ip->bottomLineIntercept;
				if (srcAnchors[1].x + srcAnchors[2].x - srcAnchors[0].x >= PROC_IMG_WIDTH)
					goto next_code_train;
				if (srcAnchors[1].y + srcAnchors[2].y - srcAnchors[0].y >= PROC_IMG_HEIGHT)
					goto next_code_train;
				ip->clip(CLIPPED_IMG_MAX_HEIGHT);

#if defined(DURATION)
				boost::chrono::time_point<boost::chrono::system_clock> clipEnd = boost::chrono::system_clock::now();
#if defined(GUI)
				sprintf(str, "clip %.0f us",
				        ((clipEnd - clipStart).count() * (double) boost::chrono::system_clock::period::num
				                / boost::chrono::system_clock::period::den) * 1e6);
				cvPutText(view, str, cvPoint(10 + 200 * row, 60), &font1, cvScalar(155, 155, 155, 0));
#else
				std::cout << "clip " << (int)( ((clipEnd - clipStart).count() * (double) boost::chrono::system_clock::period::num
								/ boost::chrono::system_clock::period::den) * 1e6) << " us\n";
#endif
#endif

#if defined(IMAGE_PATH)
				std::stringstream ss;
				ss << DIRECTORY"/clipped" << row << ".txt";
				FILE *fp = fopen(ss.str().c_str(), "w");
				const int *clipped = ip->clipped;
				for (int i = 0; i < CLIPPED_IMG_MAX_HEIGHT; i++) {
					for (int j = 0; j < CLIPPED_IMG_WIDTH; j++, clipped++)
						fprintf(fp, "%d ", *clipped);
					fprintf(fp, "\n");
				}
				fclose(fp);
#endif

#if defined(DURATION)
				boost::chrono::time_point<boost::chrono::system_clock> filterStart = boost::chrono::system_clock::now();
#endif
				/* convolution of symbols and clipped */
				ip->filter2d();

#if defined(DURATION)
				boost::chrono::time_point<boost::chrono::system_clock> filterEnd = boost::chrono::system_clock::now();
#if defined(GUI)
				sprintf(str, "filter %.0f us",
				        ((filterEnd - filterStart).count() * (double) boost::chrono::system_clock::period::num
				                / boost::chrono::system_clock::period::den) * 1e6);
				cvPutText(view, str, cvPoint(10 + 200 * row, 80), &font1, cvScalar(155, 155, 155, 0));
#else
				std::cout << "filter " << (int)( ((filterEnd - filterStart).count() * (double) boost::chrono::system_clock::period::num
								/ boost::chrono::system_clock::period::den) * 1e6) << " us\n";
#endif
#endif

#if defined(IMAGE_PATH)
				{
					std::stringstream ss;
					ss << DIRECTORY"/response" << row << ".txt";
					FILE *fp = fopen(ss.str().c_str(), "w");
					for (int i = 0; i < (int) params.N; i++) {
						for (int j = 0; j < CLIPPED_IMG_MAX_HEIGHT - CLIPPED_IMG_WIDTH + 1; j++)
							fprintf(fp, "%d ", ip->response[j][i]);
						fprintf(fp, "\n");
					}
					fclose(fp);
				}
				{
					std::stringstream ss;
					ss << DIRECTORY"/max_response" << row << ".txt";
					FILE *fp = fopen(ss.str().c_str(), "w");
					for (int j = 0; j < CLIPPED_IMG_MAX_HEIGHT - CLIPPED_IMG_WIDTH + 1; j++)
						fprintf(fp, "%d ", ip->max_response_val[j]);
					fclose(fp);
				}
#endif

				std::cout << "row " << row << ": \"";

				ip->cursor = ip->min_hill_height = 0;

				for (int k = -rowFinderSlashCount, cursor, hill_height = 0; k < (int) params.C; k++) {
					if (!(cursor = ip->nextMax(&sym[k])))
						goto next_code_row;
					if (k < 0 && sym[k] != 0) {
						k--;
						continue;
					}
					std::cout << AlphaCode::chars[sym[k]];
					if (k < 0) {
// 						if(sym[k] != 0)
// 							goto next_code_row;
						hill_height += ip->max_response_val[cursor] / rowFinderSlashCount;
						if (k == -1)
							ip->min_hill_height = hill_height;
					}
					ip->cursor = cursor + 1.3 * maximWinSize;
				}

				detected_code = alphaCode->add_row(row, sym);

				next_code_row: ;
				std::cout << "\"\n";
			}

			if (detected_code) {
				if (alphaCode->seed != AlphaCode::initial_seed) {
					std::cout << KGRN"**** found ticket no. " << detected_code << " ****"RESET"\n";
#if defined(LIVE)
					lastDetectTime = boost::chrono::system_clock::now();
					detected = true;
#endif
					if (!verify_ticket(detected_code)) {
						std::cout << KGRN"ticket is accepted. now opening the door..."RESET"\n";
						add_ticket(detected_code);
#if defined(LIVE)
						boost::thread t(blink, 1);
#endif

					} else {
						std::cout << KRED"ticket is not acceptable. it is already punched."RESET"\n";
#if defined(LIVE)
						boost::thread t(blink, 0);
#endif
					}
				}

				goto break_all_ops;
			}

			next_finder_train: ;
#if defined(DURATION)
			{
				boost::chrono::time_point<boost::chrono::system_clock> getRowsEnd = boost::chrono::system_clock::now();
#if defined(GUI)
				sprintf(str, "extractRows %.0f us",
				        ((getRowsEnd - getRowsStart).count() * (double) boost::chrono::system_clock::period::num
				                / boost::chrono::system_clock::period::den) * 1e6);
				cvPutText(view, str, cvPoint(10, 40), &font1, cvScalar(155, 155, 155, 0));
#else
				std::cout << "extractRows " << (int)(((getRowsEnd - getRowsStart).count() * (double) boost::chrono::system_clock::period::num
								/ boost::chrono::system_clock::period::den) * 1e6) << " us\n";
#endif
			}
#endif
		}
#if !defined(USE_HOUGH)
//		next_orientation: ;
#endif
	}
#if defined(DURATION)
	{
		boost::chrono::time_point<boost::chrono::system_clock> scanEnd = boost::chrono::system_clock::now();
#if defined(GUI)
		sprintf(str, "scan %.0f us",
		        ((scanEnd - scanStart).count() * (double) boost::chrono::system_clock::period::num
		                / boost::chrono::system_clock::period::den) * 1e6);
		cvPutText(view, str, cvPoint(10, 20), &font1, cvScalar(155, 155, 155, 0));
#else
		std::cout << "scan " << (int)(((scanEnd - scanStart).count() * (double) boost::chrono::system_clock::period::num
						/ boost::chrono::system_clock::period::den) * 1e6) << " us\n";
#endif
	}
#endif

	break_all_ops: ;

#if defined(DURATION) && (defined(LIVE) || defined(VIDEO_PATH))
#if defined (GUI)
	sprintf(str, "%.2f fps",
			(double) frameCounter / (boost::chrono::system_clock::now() - start).count()
			/ (double) boost::chrono::system_clock::period::num * boost::chrono::system_clock::period::den);
	cvPutText(view, str, cvPoint(10, PROC_IMG_HEIGHT - 20), &font2, cvScalar(55, 155, 155, 0));
#else
//	std::cout << (int)((double) frameCounter / (boost::chrono::system_clock::now() - start).count()
//			/ (double) boost::chrono::system_clock::period::num * boost::chrono::system_clock::period::den) << " fps\n";
#endif
#endif

#if defined(GUI)
	cvShowImage("Y", view);
	cvShowImage("binarized", ip->binarized);
//	cvShowImage("clipped", ip->clipped);
//	cvShowImage("slashDominant", ip->slashDominant);
//	cvShowImage("smooth", ip->smooth);
//	cvShowImage("morphed", ip->morphed);
//	cvShowImage("polished", ip->polished);
#if defined(IMAGE_PATH)
	cvSaveImage(DIRECTORY"/frame.png", view);
//	cvSaveImage(DIRECTORY"/clipped.bmp", ip->clipped);
#endif
#if defined(GUI) && defined(IMAGE_PATH)
	cvSaveImage(DIRECTORY"/binarized.png", ip->binarized);
#endif

#if defined(LIVE)
	int inputKey = cvWaitKey(1);
#else
	int inputKey = cvWaitKey(1000) & 0xff;
#endif

	respond: ;
	switch (inputKey & 0xff)
	{
#if defined(VIDEO_PATH) || defined(LIVE)
	case 27:
	exit_loop = 1;
	break;
#endif

	case ' ':
		inputKey = cvWaitKey(0);
		if ((inputKey & 0xff) != ' ')
			goto respond;
		break;

#if defined(VIDEO_PATH)
		case 's':
		{
			std::stringstream ss;
			ss << DIRECTORY"/frame" << frameCounter << ".yuv";
			std::string s = ss.str();
			FILE *fp = fopen(s.c_str(), "wb");
			fwrite(frame, 1, bpi * INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT, fp);
			fclose(fp);
			std::cout << "frame saved as " << s << std::endl;
			break;
		}

		case 0x51: // left arrow
		fseek(fp, -2 * bpi * INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT, SEEK_CUR);
		break;

		case 0x53:// right arrow
		break;
#endif
	}
#endif // GUI
}

void finalize()
{
#if defined(LIVE) || defined(VIDEO_PATH)
	end = boost::chrono::system_clock::now();
	double duration = (end - start).count() * (double) boost::chrono::system_clock::period::num
	/ boost::chrono::system_clock::period::den;
	std::cout << KCYN << (frameCounter / duration) << " frames per second"RESET"\n";
#endif
#if defined(GUI)
	cvReleaseImage(&view);
#endif
}
#endif // SCAN

void printUsage(const char *executablePath)
{
	std::cout << "usage:\n";
#if defined(GENERATE)
	std::cout << executablePath
	<< " [{-n|--input-number} input_number]" << std::endl;
#endif
}

int main(int argc, char **argv)
{
#if defined(LIVE)
	new_process:;
#endif

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printUsage(argv[0]);
			return 0;
		}
	}

	if (params.C > MAX_C) {
		std::cerr << "bad parameters" << std::endl;
		exit(-1);
	}

#if defined(GENERATE)
	uint64_t input_number = 0;
	uint64_t input_number_ = 0;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--input-number") || !strcmp(argv[i], "-n")) {
			if (++i >= argc) {
				printUsage(argv[0]);
				return -1;
			}
			input_number = atoi(argv[i]);
			input_number_ = input_number;
		}
	}

	alphaCode = new AlphaCode(params.N, params.C, params.L, params.R, params.D);

	const uint64_t count = (uint64_t) pow((double) params.N, (double) (params.L * (params.C - params.R)));
	const uint64_t Y = 19;
	const uint64_t X = (uint64_t) ceil((double) count / (double) Y);

//	if(event_id == alphaCode->initial_seed) {
//		std::cerr << "event ID is not specified" << std::endl;
//		printUsage(argv[0]);
//		exit(-1);
//	}

	std::stringstream ext;
	ext << "-N" << params.N << "C" << params.C << "R" << params.R << "D" << params.D << "L" << params.L;

	std::stringstream lastCodeFile;
	lastCodeFile << "last-code" << ext.str() << "E" << event_id;

	uint64_t x = 0, y = 0;

	if(!input_number_)
	{
		FILE *fp = fopen(lastCodeFile.str().c_str(), "rb");
		if (fp) {
			fread(&x, sizeof(x), 1, fp);
			fread(&y, sizeof(y), 1, fp);
			fclose(fp);
		}
		for(int t = 0; t < 2; t++)
		{
			y++;
			if(y >= Y) {
				y = 0;
				x++;
			}
			input_number = y*X + x;

			if(input_number < count)
			break;
		}
	}

	alphaCode->seed = event_id;

	if(input_number >= count) {
		std::cerr << "input number is out of bound\n";
		exit(-1);
	}

	std::string code = alphaCode->generate(input_number);

	std::cout << "ticket number " << input_number << " is:" << std::endl << std::endl;
	std::cout << code << std::endl << std::endl;

	/* verify code integrity */
	std::vector< std::vector<unsigned int> > row;
	row.resize(params.L);
	for (int r = -1, i = 0; r < (int) params.L; r++) {
		if(r >= 0)
		row[r].resize(params.C);
		i += rowFinderSlashCount + spaceBeforeRow;
		unsigned int *row_ = r >= 0 ? &row[r][0] : 0;
		while (i < (int) code.length()-1 && code[i] != '\n' && code[i] != 0) {
			i++;
			if (r < 0)
			continue;
			for (int j = 0; j < (int) params.N; j++) {
				if (code[i] == AlphaCode::chars[j]) {
					*row_++ = j;
					break;
				}
			}
		}
	}
	uint64_t ticketNumber = -1;
	for (int r = 0; r < (int) params.L; r++)
	ticketNumber = alphaCode->add_row(r, &row[r][0]);
//	std::cout << ticketNumber << std::endl;

	if (ticketNumber != input_number) {
		std::cerr << "bug encountered in alphacode" << ext.str() << "E" << event_id
		<< "T" << input_number << std::endl;
	}

	/* save last code index */
	if(!input_number_) {
		FILE *fp = fopen(lastCodeFile.str().c_str(), "wb");
		fwrite(&x, sizeof(x), 1, fp);
		fwrite(&y, sizeof(y), 1, fp);
		fclose(fp);
	}

#else
	alphaCode = new AlphaCode(params.N, params.C, params.L, params.R, params.D);

	if (sqlite3_open("tickets.db", &db)) {
		std::cerr << KRED << sqlite3_errmsg(db) << RESET"\n";
		db = 0;
	}

	alphaCode->seed = event_id;
	create_punch_table(alphaCode->seed);
	std::cout << KBLU"(N,C,R,D,L,E) = (" << params.N << ',' << params.C << ',' << params.R << ',' << params.D << ','
	        << params.L << ',' << event_id << ')' << RESET"\n";

#if defined(LIVE)
	main_(argc, argv);

#elif defined(VIDEO_PATH)
	char* frame = new char[bpi * INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT];
	CvCapture *cap = cvCreateFileCapture(VIDEO_PATH);
	if (!cap)
	fp = fopen(VIDEO_PATH, "rb");
	init();

#elif defined(IMAGE_PATH)
	char *frame = new char[bpi * INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT];
	IplImage *image = cvLoadImage(IMAGE_PATH, 0);
	if (image) {
		IplImage *smooth = cvCloneImage(image);
		cvSmooth(image, smooth, CV_GAUSSIAN, 3, 3);
		for (int k = 0; k < INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT; k++)
			frame[bpi * k] = smooth->imageData[k];
	} else { // .yuv image
		FILE *fp = fopen(IMAGE_PATH, "rb");
		fread(frame, 1, bpi * INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT, fp);
		fclose(fp);
	}
	init();
#endif

#if defined(VIDEO_PATH)
	loopStart:;
	if (!cap) {
		fread(frame, 1, bpi * INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT, fp);
	} else {
		IplImage *image = cvQueryFrame(cap);
		for (int k = 0; k < INPUT_IMG_WIDTH * INPUT_IMG_HEIGHT; k++)
		frame[bpi * k] = image->imageData[k];
	}
#endif

#if defined(VIDEO_PATH) || defined(IMAGE_PATH)
	process(frame);
#endif

#if defined(VIDEO_PATH) || defined(LIVE)
	if (exit_loop)
	goto loopBreak;
#endif

#if defined(VIDEO_PATH)
	if (cap) {
		const int idx = cvGetCaptureProperty(cap, CV_CAP_PROP_POS_FRAMES);
		const int length = cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_COUNT);
		if (idx < length)
		goto loopStart;
	} else if (!feof(fp))
	goto loopStart;
#endif

#if defined(VIDEO_PATH) || defined(LIVE)
	loopBreak:;
#endif

#if defined(VIDEO_PATH)
	fclose(fp);
	delete[] frame;
#endif

#if defined(IMAGE_PATH) && defined(GUI)
	cvWaitKey(0);
#endif

#if defined(SCAN)
	delete ip;
#endif
	delete alphaCode;

	if (db)
		sqlite3_close(db);

#endif // GENERATE

#if defined(LIVE)
	int pid;
	if (fork_process) {
		pid = fork();
		if (!pid) { // child process
			boost::this_thread::sleep(boost::posix_time::seconds(2));
			fork_process = false;
			exit_loop = false;
			argc = 0;
			goto new_process;

		} else { // parent process
			while(0 < waitpid(pid, 0, 0));// wait for child to terminate
		}
	}
#endif

	return 0;
}

#if defined(LIVE)
void blink(int status)
{
	switch (status)
	{
	case 0:
		for (int i = 0; i < 3; i++) {
			system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr0/brightness");
			system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr1/brightness");
			system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr3/brightness");
			boost::this_thread::sleep(boost::posix_time::milliseconds(200));
			system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr0/brightness");
			system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr1/brightness");
			system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr3/brightness");
			boost::this_thread::sleep(boost::posix_time::milliseconds(200));
		}
		break;
	case 1:
		system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr0/brightness");
		system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr1/brightness");
		system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr3/brightness");
		boost::this_thread::sleep(boost::posix_time::milliseconds(500));
		system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr0/brightness");
		system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr1/brightness");
		system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr3/brightness");
		break;
	case 2:
		system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr0/brightness");
		system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr1/brightness");
		system("echo 1 > /sys/class/leds/beaglebone\\:green\\:usr3/brightness");
		boost::this_thread::sleep(boost::posix_time::seconds(2));
		system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr0/brightness");
		system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr1/brightness");
		system("echo 0 > /sys/class/leds/beaglebone\\:green\\:usr3/brightness");
		break;
	}
}
#endif
