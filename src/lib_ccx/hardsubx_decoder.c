#include "lib_ccx.h"
#include "utility.h"

#ifdef ENABLE_HARDSUBX
//TODO: Correct FFMpeg integration
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include "allheaders.h"
#include "hardsubx.h"
#include "capi.h"

char* _process_frame_white_basic(struct lib_hardsubx_ctx *ctx, AVFrame *frame, int width, int height, int index)
{
	//mprint("frame : %04d\n", index);
	PIX *im;
	PIX *edge_im;
	PIX *lum_im;
	PIX *feat_im;
	char *subtitle_text=NULL;
	im = pixCreate(width,height,32);
	lum_im = pixCreate(width,height,32);
	feat_im = pixCreate(width,height,32);
	int i,j;
	for(i=(3*height)/4;i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			int p=j*3+i*frame->linesize[0];
			int r=frame->data[0][p];
			int g=frame->data[0][p+1];
			int b=frame->data[0][p+2];
			pixSetRGBPixel(im,j,i,r,g,b);
			float L,A,B;
			rgb_to_lab((float)r,(float)g,(float)b,&L,&A,&B);
			if(L > ctx->lum_thresh)
				pixSetRGBPixel(lum_im,j,i,255,255,255);
			else
				pixSetRGBPixel(lum_im,j,i,0,0,0);
		}
	}

	//Handle the edge image
	edge_im = pixCreate(width,height,8);
	edge_im = pixConvertRGBToGray(im,0.0,0.0,0.0);
	edge_im = pixSobelEdgeFilter(edge_im, L_VERTICAL_EDGES);
	edge_im = pixDilateGray(edge_im, 21, 11);
	edge_im = pixThresholdToBinary(edge_im,50);

	for(i=3*(height/4);i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			unsigned int p1,p2,p3;
			pixGetPixel(edge_im,j,i,&p1);
			// pixGetPixel(pixd,j,i,&p2);
			pixGetPixel(lum_im,j,i,&p3);
			if(p1==0&&p3>0)
				pixSetRGBPixel(feat_im,j,i,255,255,255);
			else
				pixSetRGBPixel(feat_im,j,i,0,0,0);
		}
	}

	if(ctx->detect_italics)
	{
		ctx->ocr_mode = HARDSUBX_OCRMODE_WORD;
	}

	// TESSERACT OCR FOR THE FRAME HERE
	switch(ctx->ocr_mode)
	{
		case HARDSUBX_OCRMODE_WORD:
			if(ctx->conf_thresh > 0)
				subtitle_text = get_ocr_text_wordwise_threshold(ctx, lum_im, ctx->conf_thresh);
			else
				subtitle_text = get_ocr_text_wordwise(ctx, lum_im);
			break;
		case HARDSUBX_OCRMODE_LETTER:
			if(ctx->conf_thresh > 0)
				subtitle_text = get_ocr_text_letterwise_threshold(ctx, lum_im, ctx->conf_thresh);
			else
				subtitle_text = get_ocr_text_letterwise(ctx, lum_im);
			break;
		case HARDSUBX_OCRMODE_FRAME:
			if(ctx->conf_thresh > 0)
				subtitle_text = get_ocr_text_simple_threshold(ctx, lum_im, ctx->conf_thresh);
			else
				subtitle_text = get_ocr_text_simple(ctx, lum_im);
			break;
		default:
			fatal(EXIT_MALFORMED_PARAMETER,"Invalid OCR Mode");
	}

	pixDestroy(&lum_im);
	pixDestroy(&im);
	pixDestroy(&edge_im);
	pixDestroy(&feat_im);

	return subtitle_text;
}

char *_process_frame_color_basic(struct lib_hardsubx_ctx *ctx, AVFrame *frame, int width, int height, int index)
{
	char *subtitle_text=NULL;
	PIX *im;
	im = pixCreate(width,height,32);
	PIX *hue_im = pixCreate(width,height,32);

	int i,j;
	for(i=0;i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			int p=j*3+i*frame->linesize[0];
			int r=frame->data[0][p];
			int g=frame->data[0][p+1];
			int b=frame->data[0][p+2];
			pixSetRGBPixel(im,j,i,r,g,b);
			float H,S,V;
			rgb_to_hsv((float)r,(float)g,(float)b,&H,&S,&V);
			if(abs(H-ctx->hue)<20)
			{
				pixSetRGBPixel(hue_im,j,i,r,g,b);
			}
		}
	}

	PIX *edge_im = pixCreate(width,height,8),*edge_im_2 = pixCreate(width,height,8);
	edge_im = pixConvertRGBToGray(im,0.0,0.0,0.0);
	edge_im = pixSobelEdgeFilter(edge_im, L_VERTICAL_EDGES);
	edge_im = pixDilateGray(edge_im, 21, 1);
	edge_im = pixThresholdToBinary(edge_im,50);
	PIX *pixd = pixCreate(width,height,1);
	pixSauvolaBinarize(pixConvertRGBToGray(hue_im,0.0,0.0,0.0), 15, 0.3, 1, NULL, NULL, NULL, &pixd);

	edge_im_2 = pixConvertRGBToGray(hue_im,0.0,0.0,0.0);
	edge_im_2 = pixDilateGray(edge_im_2, 5, 5);

	PIX *feat_im = pixCreate(width,height,32);
	for(i=3*(height/4);i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			unsigned int p1,p2,p3,p4;
			pixGetPixel(edge_im,j,i,&p1);
			pixGetPixel(pixd,j,i,&p2);
			// pixGetPixel(hue_im,j,i,&p3);
			pixGetPixel(edge_im_2,j,i,&p4);
			if(p1==0&&p2==0&&p4>0)//if(p4>0&&p1==0)//if(p2==0&&p1==0&&p3>0)
			{
				pixSetRGBPixel(feat_im,j,i,255,255,255);
			}
		}
	}


	if(ctx->detect_italics)
	{
		ctx->ocr_mode = HARDSUBX_OCRMODE_WORD;
	}

	// TESSERACT OCR FOR THE FRAME HERE
	switch(ctx->ocr_mode)
	{
		case HARDSUBX_OCRMODE_WORD:
			if(ctx->conf_thresh > 0)
				subtitle_text = get_ocr_text_wordwise_threshold(ctx, feat_im, ctx->conf_thresh);
			else
				subtitle_text = get_ocr_text_wordwise(ctx, feat_im);
			break;
		case HARDSUBX_OCRMODE_LETTER:
			if(ctx->conf_thresh > 0)
				subtitle_text = get_ocr_text_letterwise_threshold(ctx, feat_im, ctx->conf_thresh);
			else
				subtitle_text = get_ocr_text_letterwise(ctx, feat_im);
			break;
		case HARDSUBX_OCRMODE_FRAME:
			if(ctx->conf_thresh > 0)
				subtitle_text = get_ocr_text_simple_threshold(ctx, feat_im, ctx->conf_thresh);
			else
				subtitle_text = get_ocr_text_simple(ctx, feat_im);
			break;
		default:
			fatal(EXIT_MALFORMED_PARAMETER,"Invalid OCR Mode");
	}

	pixDestroy(&feat_im);
	pixDestroy(&im);
	pixDestroy(&edge_im);
	pixDestroy(&hue_im);

	return subtitle_text;
}

void _display_frame(struct lib_hardsubx_ctx *ctx, AVFrame *frame, int width, int height, int timestamp)
{
	// Debug: Display the frame after processing
	PIX *im;
	im = pixCreate(width,height,32);
	PIX *hue_im = pixCreate(width,height,32);

	int i,j;
	for(i=0;i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			int p=j*3+i*frame->linesize[0];
			int r=frame->data[0][p];
			int g=frame->data[0][p+1];
			int b=frame->data[0][p+2];
			pixSetRGBPixel(im,j,i,r,g,b);
			float H,S,V;
			rgb_to_hsv((float)r,(float)g,(float)b,&H,&S,&V);
			if(abs(H-ctx->hue)<20)
			{
				pixSetRGBPixel(hue_im,j,i,r,g,b);
			}
		}
	}

	PIX *edge_im = pixCreate(width,height,8),*edge_im_2 = pixCreate(width,height,8);
	edge_im = pixConvertRGBToGray(im,0.0,0.0,0.0);
	edge_im = pixSobelEdgeFilter(edge_im, L_VERTICAL_EDGES);
	edge_im = pixDilateGray(edge_im, 21, 1);
	edge_im = pixThresholdToBinary(edge_im,50);
	PIX *pixd = pixCreate(width,height,1);
	pixSauvolaBinarize(pixConvertRGBToGray(hue_im,0.0,0.0,0.0), 15, 0.3, 1, NULL, NULL, NULL, &pixd);

	edge_im_2 = pixConvertRGBToGray(hue_im,0.0,0.0,0.0);
	edge_im_2 = pixDilateGray(edge_im_2, 5, 5);

	PIX *feat_im = pixCreate(width,height,32);
	for(i=3*(height/4);i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			unsigned int p1,p2,p3,p4;
			pixGetPixel(edge_im,j,i,&p1);
			pixGetPixel(pixd,j,i,&p2);
			// pixGetPixel(hue_im,j,i,&p3);
			pixGetPixel(edge_im_2,j,i,&p4);
			if(p1==0&&p2==0&&p4>0)//if(p4>0&&p1==0)//if(p2==0&&p1==0&&p3>0)
			{
				pixSetRGBPixel(feat_im,j,i,255,255,255);
			}
		}
	}

	char *txt=NULL;
	// txt = get_ocr_text_simple(ctx, feat_im);
	// txt=get_ocr_text_wordwise_threshold(ctx, feat_im, ctx->conf_thresh);
	// if(txt != NULL)mprint("%s\n", txt);

	pixDestroy(&im);
	pixDestroy(&edge_im);
	pixDestroy(&feat_im);
	pixDestroy(&edge_im_2);
	pixDestroy(&pixd);
}

char* _process_frame_tickertext(struct lib_hardsubx_ctx *ctx, AVFrame *frame, int width, int height, int index)
{
	PIX *im;
	PIX *edge_im;
	PIX *lum_im;
	PIX *feat_im;
	char *subtitle_text=NULL;
	im = pixCreate(width,height,32);
	lum_im = pixCreate(width,height,32);
	feat_im = pixCreate(width,height,32);
	int i,j;
	for(i=(92*height)/100;i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			int p=j*3+i*frame->linesize[0];
			int r=frame->data[0][p];
			int g=frame->data[0][p+1];
			int b=frame->data[0][p+2];
			pixSetRGBPixel(im,j,i,r,g,b);
			float L,A,B;
			rgb_to_lab((float)r,(float)g,(float)b,&L,&A,&B);
			if(L > ctx->lum_thresh)
				pixSetRGBPixel(lum_im,j,i,255,255,255);
			else
				pixSetRGBPixel(lum_im,j,i,0,0,0);
		}
	}

	//Handle the edge image
	edge_im = pixCreate(width,height,8);
	edge_im = pixConvertRGBToGray(im,0.0,0.0,0.0);
	edge_im = pixSobelEdgeFilter(edge_im, L_VERTICAL_EDGES);
	edge_im = pixDilateGray(edge_im, 21, 11);
	edge_im = pixThresholdToBinary(edge_im,50);

	for(i=92*(height/100);i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			unsigned int p1,p2,p3;
			pixGetPixel(edge_im,j,i,&p1);
			// pixGetPixel(pixd,j,i,&p2);
			pixGetPixel(lum_im,j,i,&p3);
			if(p1==0&&p3>0)
				pixSetRGBPixel(feat_im,j,i,255,255,255);
			else
				pixSetRGBPixel(feat_im,j,i,0,0,0);
		}
	}

	// Tesseract OCR for the ticker text here
	subtitle_text = get_ocr_text_simple(ctx, lum_im);
	char write_path[100];
	sprintf(write_path,"./lum_im%04d.jpg",index);
	pixWrite(write_path,lum_im,IFF_JFIF_JPEG);
	sprintf(write_path,"./im%04d.jpg",index);
	pixWrite(write_path,im,IFF_JFIF_JPEG);

	pixDestroy(&lum_im);
	pixDestroy(&im);
	pixDestroy(&edge_im);
	pixDestroy(&feat_im);

	return subtitle_text;
}

int hardsubx_process_frames_tickertext(struct lib_hardsubx_ctx *ctx, struct encoder_ctx *enc_ctx)
{
	// Search for ticker text at the bottom of the screen, such as in Russia TV1 or stock prices
	int got_frame;
	int cur_sec,total_sec,progress;
	int frame_number = 0;
	char *ticker_text = NULL;

	while(av_read_frame(ctx->format_ctx, &ctx->packet)>=0)
	{
		if(ctx->packet.stream_index == ctx->video_stream_id)
		{
			frame_number++;
			//Decode the video stream packet
			avcodec_decode_video2(ctx->codec_ctx, ctx->frame, &got_frame, &ctx->packet);
			if(got_frame && frame_number % 1000 == 0)
			{
				// sws_scale is used to convert the pixel format to RGB24 from all other cases
				sws_scale(
						ctx->sws_ctx,
						(uint8_t const * const *)ctx->frame->data,
						ctx->frame->linesize,
						0,
						ctx->codec_ctx->height,
						ctx->rgb_frame->data,
						ctx->rgb_frame->linesize
					);

				ticker_text = _process_frame_tickertext(ctx,ctx->rgb_frame,ctx->codec_ctx->width,ctx->codec_ctx->height,frame_number);
				mprint("frame_number: %d\n", frame_number);

				if(strlen(ticker_text)>0)mprint("%s\n", ticker_text);

				cur_sec = (int)convert_pts_to_s(ctx->packet.pts, ctx->format_ctx->streams[ctx->video_stream_id]->time_base);
				total_sec = (int)convert_pts_to_s(ctx->format_ctx->duration, AV_TIME_BASE_Q);
				progress = (cur_sec*100)/total_sec;
				activity_progress(progress,cur_sec/60,cur_sec%60);
			}
		}
	}
	activity_progress(100,cur_sec/60,cur_sec%60);
	return 0;
}

int hardsubx_process_frames_linear(struct lib_hardsubx_ctx *ctx, struct encoder_ctx *enc_ctx)
{
	// Do an exhaustive linear search over the video
	int got_frame;
	int dist;
	int cur_sec,total_sec,progress;
	int frame_number = 0;
	int64_t begin_time = 0,end_time = 0,prev_packet_pts = 0;
	char *subtitle_text=NULL;
	char *prev_subtitle_text=NULL;

	while(av_read_frame(ctx->format_ctx, &ctx->packet)>=0)
	{
		if(ctx->packet.stream_index == ctx->video_stream_id)
		{
			frame_number++;

			//Decode the video stream packet
			avcodec_decode_video2(ctx->codec_ctx, ctx->frame, &got_frame, &ctx->packet);

			if(got_frame && frame_number % 25 == 0)
			{
				hardsubx_process_single_frame(&ctx, &prev_packet_pts, &begin_time, &end_time, &prev_subtitle_text, enc_ctx);
			}
		}
		av_packet_unref(&ctx->packet);
	}

	add_cc_sub_text(ctx->dec_sub, prev_subtitle_text, begin_time, end_time, "", "BURN", CCX_ENC_UTF_8);
	encode_sub(enc_ctx, ctx->dec_sub);
	activity_progress(100,cur_sec/60,cur_sec%60);

}

void hardsubx_process_single_frame(struct lib_hardsubx_ctx **ctxp, LLONG *prev_packet_pts, LLONG *begin_time, LLONG *end_time, char **prev_subtitle_text, struct encoder_ctx *enc_ctx, int *changed)
{
	int dist;
	int cur_sec,total_sec,progress;
	static int frame_number = 0;
	char *subtitle_text=NULL;
	struct lib_hardsubx_ctx *ctx = *ctxp;
	//struct encoder_ctx *enc_ctx = *enc_ctxp;

	float diff = (float)convert_pts_to_ms(ctx->packet.pts - *prev_packet_pts, ctx->format_ctx->streams[ctx->video_stream_id]->time_base);
				if(abs(diff) < 1000*ctx->min_sub_duration) //If the minimum duration of a subtitle line is exceeded, process packet
					return;

				// sws_scale is used to convert the pixel format to RGB24 from all other cases
				sws_scale(
						ctx->sws_ctx,
						(uint8_t const * const *)ctx->frame->data,
						ctx->frame->linesize,
						0,
						ctx->codec_ctx->height,
						ctx->rgb_frame->data,
						ctx->rgb_frame->linesize
					);


				// Send the frame to other functions for processing
				if(ctx->subcolor==HARDSUBX_COLOR_WHITE)
				{
					subtitle_text = _process_frame_white_basic(ctx,ctx->rgb_frame,ctx->codec_ctx->width,ctx->codec_ctx->height,frame_number);
				}
				else
				{
					subtitle_text = _process_frame_color_basic(ctx, ctx->rgb_frame, ctx->codec_ctx->width,ctx->codec_ctx->height,frame_number);
				}
				_display_frame(ctx, ctx->rgb_frame,ctx->codec_ctx->width,ctx->codec_ctx->height,frame_number);

				cur_sec = (int)convert_pts_to_s(ctx->packet.pts, ctx->format_ctx->streams[ctx->video_stream_id]->time_base);
				total_sec = (int)convert_pts_to_s(ctx->format_ctx->duration, AV_TIME_BASE_Q);
				progress = (cur_sec*100)/total_sec;
				//activity_progress(progress,cur_sec/60,cur_sec%60);

				*end_time = convert_pts_to_ms(ctx->packet.pts, ctx->format_ctx->streams[ctx->video_stream_id]->time_base);
				mprint("\nEND_TIME HARDSUB SINGLE: %ld\n", *end_time);

				if(subtitle_text==NULL)
					return;
				if(!strlen(subtitle_text))
					return;
				char *double_enter = strstr(subtitle_text,"\n\n");
				if(double_enter!=NULL)
					*(double_enter)='\0';
				//subtitle_text = prune_string(subtitle_text);
				printf("No segfault\n");
				if(*prev_subtitle_text)
				{
					printf("No segfault1\n");
					//TODO: Encode text with highest confidence
					dist = edit_distance(subtitle_text, *prev_subtitle_text, strlen(subtitle_text), strlen(*prev_subtitle_text));

					printf("No segfault2\n");
					if(dist > (0.2 * fmin(strlen(subtitle_text), strlen(*prev_subtitle_text))))
					{
						mprint("\n HARDSUBS::: %s:::\n", *prev_subtitle_text);
						add_cc_sub_text(ctx->dec_sub, *prev_subtitle_text, *begin_time, *end_time, "", "BURN", CCX_ENC_UTF_8);
						encode_sub(enc_ctx, ctx->dec_sub);
						*begin_time = *end_time + 1;
						printf("No segfault3\n");
					}
				}

				// if(ctx->conf_thresh > 0)
				// {
				// 	if(ctx->cur_conf >= ctx->prev_conf)
				// 	{
				// 		prev_subtitle_text = strdup(subtitle_text);
				// 		ctx->prev_conf = ctx->cur_conf;
				// 	}
				// }
				// else
				// {
				// 	prev_subtitle_text = strdup(subtitle_text);
				// }
				*prev_subtitle_text = strdup(subtitle_text);
				*prev_packet_pts = ctx->packet.pts;
}

void hardsubx_post_cc(struct lib_hardsubx_ctx **ctxp, struct encoder_ctx *enc_ctx, ULLONG start, ULLONG end, char **prev_hardsub)
{
	int got_frame;
	int dist;
	int cur_sec,total_sec,progress;
	int frame_number = 0;
	int64_t begin_time = start,end_time = 0,prev_packet_pts = 0;
	char *subtitle_text=NULL;
	char *prev_subtitle_text = *prev_hardsub;
	struct lib_hardsubx_ctx *ctx = *ctxp;
	int changed = 0;

	printf("Address1\n");
	printf("Address of prev_hardsub: %p\n", &prev_hardsub);
	printf("Address of prev_subtitle_text: %p\n", &prev_subtitle_text);
	if(start == -1) {
		// Get the required media attributes and initialize structures
		av_register_all();
		
		if(avformat_open_input(&ctx->format_ctx, ctx->inputfile[0], NULL, NULL)!=0)
		{
			fatal (EXIT_READ_ERROR, "Error reading input file!\n");
		}

		if(avformat_find_stream_info(ctx->format_ctx, NULL)<0)
		{
			fatal (EXIT_READ_ERROR, "Error reading input stream!\n");
		}

		// Important call in order to determine media information using ffmpeg
		// TODO: Handle multiple inputs
		av_dump_format(ctx->format_ctx, 0, ctx->inputfile[0], 0);
		

		ctx->video_stream_id = -1;
		for(int i = 0; i < ctx->format_ctx->nb_streams; i++)
		{
			if(ctx->format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				ctx->video_stream_id = i;
				break;
			}
		}
		if(ctx->video_stream_id == -1)
		{
			fatal (EXIT_READ_ERROR, "Video Stream not found!\n");
		}

		ctx->codec_ctx = ctx->format_ctx->streams[ctx->video_stream_id]->codec;
		ctx->codec = avcodec_find_decoder(ctx->codec_ctx->codec_id);
		if(ctx->codec == NULL)
		{
			fatal (EXIT_READ_ERROR, "Input codec is not supported!\n");
		}

		if(avcodec_open2(ctx->codec_ctx, ctx->codec, &ctx->options_dict) < 0)
		{
			fatal (EXIT_READ_ERROR, "Error opening input codec!\n");
		}

		ctx->frame = av_frame_alloc();
		ctx->rgb_frame = av_frame_alloc();
		if(!ctx->frame || !ctx->rgb_frame)
		{
			fatal(EXIT_NOT_ENOUGH_MEMORY, "Not enough memory to initialize frame!");
		}

		int frame_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, ctx->codec_ctx->width, ctx->codec_ctx->height, 16);
		ctx->rgb_buffer = (uint8_t *)av_malloc(frame_bytes*sizeof(uint8_t));
		
		ctx->sws_ctx = sws_getContext(
				ctx->codec_ctx->width,
				ctx->codec_ctx->height,
				ctx->codec_ctx->pix_fmt,
				ctx->codec_ctx->width,
				ctx->codec_ctx->height,
				AV_PIX_FMT_RGB24,
				SWS_BILINEAR,
				NULL,NULL,NULL
			);

		av_image_fill_arrays(ctx->rgb_frame->data, ctx->rgb_frame->linesize, ctx->rgb_buffer, AV_PIX_FMT_RGB24, ctx->codec_ctx->width, ctx->codec_ctx->height, 1);
		return NULL;
	}
	// Do an exhaustive linear search over the video
	LLONG start_pts = start*90;
    //AVRational bq = {1,1000};
	//LLONG start_pts = av_rescale_q(start, bq, ctx->format_ctx->streams[ctx->video_stream_id]->time_base);
	mprint("\n\nSTARTPTS: %ld\n\n", start_pts);
	mprint("\n\nStart: %ld End: %ld\n\n", start, end);
	if(av_seek_frame(ctx->format_ctx, ctx->video_stream_id, start_pts, AVSEEK_FLAG_ANY) < 0)
		return NULL;
	//avcodec_flush_buffers(ctx->codec_ctx);
	//if(avformat_seek_file(ctx->format_ctx, ctx->video_stream_id, 0, start_pts, start_pts, AVSEEK_FLAG_FRAME) < 0)
	//	return NULL;
	while(av_read_frame(ctx->format_ctx, &ctx->packet)>=0)
	{
		if(ctx->packet.stream_index == ctx->video_stream_id)
		{
			frame_number++;
			//Decode the video stream packet
			avcodec_decode_video2(ctx->codec_ctx, ctx->frame, &got_frame, &ctx->packet);

			if(got_frame && frame_number % 25 == 0)
			{
				mprint("Packet info: %lld pts %lld pos\n", ctx->packet.pts, ctx->packet.pos);
				end_time = convert_pts_to_ms(ctx->packet.pts, ctx->format_ctx->streams[ctx->video_stream_id]->time_base);
				if(end_time >= end) {
					av_packet_unref(&ctx->packet);
					break;
				}
				hardsubx_process_single_frame(&ctx, &prev_packet_pts, &begin_time, &end_time, &prev_subtitle_text, enc_ctx, &changed);
				printf("No segfault4\n");
			}
		}
		av_packet_unref(&ctx->packet);
	}

	// printf("No segfault5\n");
	// printf("Address2\n");
	// printf("Address of prev_hardsub: %p\n", prev_hardsub);
	// printf("Address of prev_subtitle_text: %p\n", &prev_subtitle_text);
	// if(prev_subtitle_text && strlen(prev_subtitle_text) > 0 && changed) {
	// 	add_cc_sub_text(ctx->dec_sub, prev_subtitle_text, begin_time, end_time, "", "BURN", CCX_ENC_UTF_8);
	// 	encode_sub(enc_ctx, ctx->dec_sub);
	// }
	//activity_progress(100,cur_sec/60,cur_sec%60);
	*prev_hardsub = prev_subtitle_text;
}

int hardsubx_process_frames_binary(struct lib_hardsubx_ctx *ctx)
{
	// Do a binary search over the input video for faster processing
	// mprint("Duration: %d\n", (int)ctx->format_ctx->duration);
	int got_frame;
	int seconds_time = 0;
	for(seconds_time=0;seconds_time<20;seconds_time++){
	int64_t seek_time = (int64_t)(seconds_time*AV_TIME_BASE);
	seek_time = av_rescale_q(seek_time, AV_TIME_BASE_Q, ctx->format_ctx->streams[ctx->video_stream_id]->time_base);

	int ret = av_seek_frame(ctx->format_ctx, ctx->video_stream_id, seek_time, AVSEEK_FLAG_BACKWARD);
	// mprint("%d\n", ret);
	// if(ret < 0)
	// {
	// 	mprint("seeking back\n");
	// 	ret = av_seek_frame(ctx->format_ctx, -1, seek_time, AVSEEK_FLAG_BACKWARD);
	// }
	if(ret >= 0)
	{
		while(av_read_frame(ctx->format_ctx, &ctx->packet)>=0)
		{
			if(ctx->packet.stream_index == ctx->video_stream_id)
			{
				avcodec_decode_video2(ctx->codec_ctx, ctx->frame, &got_frame, &ctx->packet);
				if(got_frame)
				{
					// mprint("%d\n", seek_time);
					if(ctx->packet.pts < seek_time)
						continue;
					// mprint("GOT FRAME: %d\n",ctx->packet.pts);
					// sws_scale is used to convert the pixel format to RGB24 from all other cases
					sws_scale(
							ctx->sws_ctx,
							(uint8_t const * const *)ctx->frame->data,
							ctx->frame->linesize,
							0,
							ctx->codec_ctx->height,
							ctx->rgb_frame->data,
							ctx->rgb_frame->linesize
						);
					// Send the frame to other functions for processing
					_display_frame(ctx, ctx->rgb_frame,ctx->codec_ctx->width,ctx->codec_ctx->height,seconds_time);
					break;
				}
			}
		}
	}
	else
	{
		mprint("Seeking to timestamp failed\n");
	}
	}
}

#endif
