// gbench FILE.flac COVER.jpg GRAPH SECONDS: runs a lavfi-complex graph (with [aid1]/[vid1] fed by amovie/movie)
// single-threaded, pulls [vo] and [ao] to their ends, prints CPU time and the frame count
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavutil/frame.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
static double cpu(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char** argv)
{
	const char* g = argv[3];
	char* full = malloc(strlen(g) + 4096);
	int art = strstr(g, "[vid1]") != NULL;
	sprintf(full, "amovie='%s':loop=1 [aid1]; %s%s%s%s; [vo] buffersink; [ao] abuffersink",
		argv[1], art ? "movie='" : "", art ? argv[2] : "", art ? "' [vid1]; " : "", g);
	AVFilterGraph* gr = avfilter_graph_alloc();
	gr->nb_threads = 1;
	if (avfilter_graph_parse_ptr(gr, full, NULL, NULL, NULL) < 0 || avfilter_graph_config(gr, NULL) < 0) { fprintf(stderr, "bad graph\n%s\n", full); return 1; }
	AVFilterContext *vs = NULL, *as = NULL;
	for (unsigned i = 0; i < gr->nb_filters; i++)
	{
		if (!strcmp(gr->filters[i]->filter->name, "buffersink")) vs = gr->filters[i];
		if (!strcmp(gr->filters[i]->filter->name, "abuffersink")) as = gr->filters[i];
	}
	double limit = atof(argv[4]);
	AVFrame* f = av_frame_alloc();
	long frames = 0; double apts = 0;
	double t0 = cpu();
	while (apts < limit)
	{
		int r = avfilter_graph_request_oldest(gr);
		while (av_buffersink_get_frame(vs, f) >= 0) { frames++; av_frame_unref(f); }
		while (av_buffersink_get_frame(as, f) >= 0)
		{
			AVRational tb = av_buffersink_get_time_base(as);
			apts = (f->pts + f->nb_samples * 0.0) * av_q2d(tb);
			av_frame_unref(f);
		}
		if (r == AVERROR_EOF) break;
	}
	double t = cpu() - t0;
	printf("%.3f %ld %.2f\n", t, frames, apts);
	return 0;
}
