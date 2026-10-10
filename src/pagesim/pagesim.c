#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pagesim.h"

#define MAX_REF 256
#define MAX_FRAMES 64

typedef struct {
    int hits;
    int faults;
} SimulationStats;


/* Find a page in the frames. */
static int find_page(const int *frames, int frame_count, int page)
{
    for (int i = 0; i < frame_count; i++) {
        if (frames[i] == page)
            return i;
    }

    return -1;
}


/* Print the current frame contents. */
static void print_frames(const int *frames, int frame_count)
{
    printf("[");

    for (int i = 0; i < frame_count; i++) {
        if (frames[i] == -1)
            printf("-");
        else
            printf("%d", frames[i]);

        if (i < frame_count - 1)
            printf(" ");
    }

    printf("]");
}


/* Check supported page replacement algorithms. */
static int is_supported_algorithm(const char *algo)
{
    return strcmp(algo, "fifo") == 0 ||
           strcmp(algo, "lru") == 0 ||
           strcmp(algo, "lfu") == 0 ||
           strcmp(algo, "clock") == 0 ||
           strcmp(algo, "hybrid") == 0 ||
           strcmp(algo, "awsf") == 0 ||
           strcmp(algo, "optimal") == 0;
}


/*
 * Parse page references separated by whitespace or commas.
 * Reject invalid input instead of silently truncating it.
 */
static int parse_references(
    const char *ref,
    int *refs,
    int *count_out)
{
    char buffer[4096];
    size_t length;
    int count = 0;

    if (ref == NULL || refs == NULL || count_out == NULL)
        return 0;

    length = strlen(ref);

    if (length == 0 || length >= sizeof(buffer)) {
        printf(
            "PageSim: reference string is empty or too long.\n"
        );
        return 0;
    }

    memcpy(buffer, ref, length + 1);

    char *cursor = buffer;

    while (*cursor != '\0') {
        while (*cursor == ',' ||
               isspace((unsigned char)*cursor)) {
            cursor++;
        }

        if (*cursor == '\0')
            break;

        errno = 0;
        char *end = NULL;

        long value = strtol(cursor, &end, 10);

        if (end == cursor ||
            errno == ERANGE ||
            value < INT_MIN ||
            value > INT_MAX ||
            (*end != '\0' &&
             *end != ',' &&
             !isspace((unsigned char)*end))) {

            printf(
                "PageSim: invalid page reference near '%s'.\n",
                cursor
            );

            return 0;
        }

        if (count >= MAX_REF) {
            printf(
                "PageSim: trace exceeds the %d-reference limit.\n",
                MAX_REF
            );

            return 0;
        }

        refs[count++] = (int)value;
        cursor = end;
    }

    if (count == 0) {
        printf(
            "PageSim: reference string contains no page IDs.\n"
        );

        return 0;
    }

    *count_out = count;

    return 1;
}


/*
 * Choose the victim frame for LRU, LFU, Clock,
 * Hybrid, or Optimal.
 *
 * FIFO replacement is handled separately.
 */
static int choose_victim(
    const char *algo,
    const int *frames,
    int frame_count,
    const int *last_used,
    const int *frequency,
    const int *arrival,
    int *reference_bits,
    int clock_hand,
    int step,
    const int *refs,
    int reference_count)
{
    int victim = 0;

    if (strcmp(algo, "fifo") == 0) {
        return -1;
    }

    /*
     * CLOCK / SECOND CHANCE
     */
    if (strcmp(algo, "clock") == 0) {
        int hand = clock_hand;
        int scanned = 0;

        while (scanned < frame_count * 2) {
            if (reference_bits[hand] == 0)
                return hand;

            reference_bits[hand] = 0;
            hand = (hand + 1) % frame_count;
            scanned++;
        }

        return clock_hand;
    }

    /*
     * LEAST RECENTLY USED
     */
    if (strcmp(algo, "lru") == 0) {
        for (int i = 1; i < frame_count; i++) {
            if (last_used[i] < last_used[victim])
                victim = i;
        }

        return victim;
    }

    /*
     * LEAST FREQUENTLY USED
     * Ties are broken by least recent use.
     */
    if (strcmp(algo, "lfu") == 0) {
        for (int i = 1; i < frame_count; i++) {
            if (frequency[i] < frequency[victim] ||
                (frequency[i] == frequency[victim] &&
                 last_used[i] < last_used[victim])) {

                victim = i;
            }
        }

        return victim;
    }

    /*
     * HYBRID REPLACEMENT
     *
     * Stage 1: Select the lowest-frequency page.
     *
     * Stage 2: Break ties using recency groups.
     *
     * Stage 3: Break remaining ties using FIFO arrival time.
     */
    if (strcmp(algo, "hybrid") == 0) {
        for (int i = 1; i < frame_count; i++) {
            int group_i =
                (step - last_used[i]) / 3;

            int group_v =
                (step - last_used[victim]) / 3;

            if (frequency[i] < frequency[victim] ||
                (frequency[i] == frequency[victim] &&
                 group_i > group_v) ||
                (frequency[i] == frequency[victim] &&
                 group_i == group_v &&
                 arrival[i] < arrival[victim])) {

                victim = i;
            }
        }

        return victim;
    }

    /*
     * OPTIMAL PAGE REPLACEMENT
     *
     * Replace the page whose next use is farthest
     * in the future, or which is never used again.
     */
    if (strcmp(algo, "optimal") == 0) {
        int farthest = -1;

        for (int i = 0; i < frame_count; i++) {
            int next_use = reference_count;

            for (int j = step + 1;
                 j < reference_count;
                 j++) {

                if (refs[j] == frames[i]) {
                    next_use = j;
                    break;
                }
            }

            if (next_use > farthest) {
                farthest = next_use;
                victim = i;
            }
        }

        return victim;
    }

    return 0;
}


/*
 * Run a page replacement simulation.
 *
 * print_steps = 1:
 *     Display the page-by-page simulation.
 *
 * print_steps = 0:
 *     Calculate counts only.
 */
/*
 * AWSF: Adaptive Working-Set Frequency.
 *
 * First prefer pages outside the active window.
 * Among those pages, evict the least frequently used;
 * break frequency ties using the oldest last access.
 *
 * If every resident page is active, fall back to LFU
 * with LRU tie-breaking.
 */
static int choose_awsf_victim(
    int frame_count,
    const int *last_used,
    const int *frequency,
    int step,
    int adaptive_window)
{
    int victim = -1;

    for (int i = 0; i < frame_count; i++) {
        int age = step - last_used[i];

        if (age >= adaptive_window) {
            if (victim == -1 ||
                frequency[i] < frequency[victim] ||
                (frequency[i] == frequency[victim] &&
                 last_used[i] < last_used[victim])) {
                victim = i;
            }
        }
    }

    if (victim != -1)
        return victim;

    victim = 0;

    for (int i = 1; i < frame_count; i++) {
        if (frequency[i] < frequency[victim] ||
            (frequency[i] == frequency[victim] &&
             last_used[i] < last_used[victim])) {
            victim = i;
        }
    }

    return victim;
}

static int simulate(
    const char *algo,
    int frame_count,
    const int *refs,
    int reference_count,
    int print_steps,
    SimulationStats *stats_out)
{
    int frames[MAX_FRAMES];
    int last_used[MAX_FRAMES];
    int frequency[MAX_FRAMES];
    int arrival[MAX_FRAMES];
    int reference_bits[MAX_FRAMES];

    int fifo_pos = 0;
    int clock_hand = 0;

    int min_window = frame_count;
    int max_window = frame_count * 4;
    int adaptive_window = frame_count * 2;
    int recent_faults = 0;
    int recent_samples = 0;

    SimulationStats stats = {0, 0};

    if (max_window > MAX_REF)
        max_window = MAX_REF;

    if (adaptive_window < min_window)
        adaptive_window = min_window;

    if (adaptive_window > max_window)
        adaptive_window = max_window;

    if (!is_supported_algorithm(algo) ||
        frame_count < 1 ||
        frame_count > MAX_FRAMES ||
        refs == NULL ||
        reference_count < 1) {

        return 0;
    }

    /*
     * Initialize all frame metadata.
     */
    for (int i = 0; i < frame_count; i++) {
        frames[i] = -1;
        last_used[i] = -1;
        frequency[i] = 0;
        arrival[i] = -1;
        reference_bits[i] = 0;
    }

    if (print_steps) {
        printf("\nStep  Page  Frames");

        for (int i = 0; i < 14; i++)
            printf(" ");

        printf("Result\n");
        printf("------------------------------------------------\n");
    }

    /*
     * Process each page reference.
     */
    for (int step = 0;
         step < reference_count;
         step++) {

        int page = refs[step];

        int position =
            find_page(frames, frame_count, page);

        int was_hit = position >= 0;

        /*
         * PAGE HIT
         */
        if (was_hit) {
            stats.hits++;

            frequency[position]++;
            last_used[position] = step;
            reference_bits[position] = 1;
        }

        /*
         * PAGE FAULT
         */
        else {
            stats.faults++;

            int victim = -1;

            /*
             * Use an empty frame first.
             */
            for (int i = 0; i < frame_count; i++) {
                if (frames[i] == -1) {
                    victim = i;
                    break;
                }
            }

            /*
             * If all frames are occupied,
             * select a victim using the algorithm.
             */
            if (victim == -1) {
                if (strcmp(algo, "fifo") == 0) {
                    victim = fifo_pos;

                    fifo_pos =
                        (fifo_pos + 1) % frame_count;
                }

                else if (strcmp(algo, "awsf") == 0) {
                    victim = choose_awsf_victim(
                        frame_count,
                        last_used,
                        frequency,
                        step,
                        adaptive_window
                    );
                }

                else {
                    victim = choose_victim(
                        algo,
                        frames,
                        frame_count,
                        last_used,
                        frequency,
                        arrival,
                        reference_bits,
                        clock_hand,
                        step,
                        refs,
                        reference_count
                    );
                }
            }

            if (victim < 0 ||
                victim >= frame_count) {

                return 0;
            }

            /*
             * Load the requested page.
             */
            frames[victim] = page;

            last_used[victim] = step;
            frequency[victim] = 1;
            arrival[victim] = step;
            reference_bits[victim] = 1;

            if (strcmp(algo, "clock") == 0) {
                clock_hand =
                    (victim + 1) % frame_count;
            }
        }

        /*
         * Display current frame state.
         */
        if (print_steps) {
            printf(
                "%-5d %-5d ",
                step + 1,
                page
            );

            print_frames(frames, frame_count);

            printf(
                "     %s\n",
                was_hit ? "HIT" : "FAULT"
            );
        }

        if (strcmp(algo, "awsf") == 0) {
            if (!was_hit)
                recent_faults++;

            recent_samples++;

            if (recent_samples == 8) {
                if (recent_faults >= 4 &&
                    adaptive_window > min_window) {
                    adaptive_window--;
                }
                else if (recent_faults <= 2 &&
                         adaptive_window < max_window) {
                    adaptive_window++;
                }

                recent_faults = 0;
                recent_samples = 0;
            }
        }
    }

    if (stats_out != NULL)
        *stats_out = stats;

    /*
     * Print final statistics.
     */
    if (print_steps) {
        double hit_ratio =
            100.0 * (double)stats.hits /
            reference_count;

        printf("------------------------------------------------\n");
        printf("Page Hits   : %d\n", stats.hits);
        printf("Page Faults : %d\n", stats.faults);
        printf("Hit Ratio   : %.2f%%\n", hit_ratio);
        printf("========================================\n");
    }

    return 1;
}


/*
 * Run a simulation without displaying every step.
 */
static int simulate_counts(
    int frame_count,
    const char *algo,
    const int *refs,
    int reference_count,
    int *hits_out,
    int *faults_out)
{
    SimulationStats stats;

    if (!simulate(
            algo,
            frame_count,
            refs,
            reference_count,
            0,
            &stats)) {

        return 0;
    }

    if (hits_out != NULL)
        *hits_out = stats.hits;

    if (faults_out != NULL)
        *faults_out = stats.faults;

    return 1;
}


/*
 * WORKING-SET ANALYSIS
 *
 * The working set at each step consists of the distinct
 * pages referenced within the most recent W references,
 * including the current reference.
 *
 * For early steps, use only references that exist.
 */
static void run_working_set(
    const int *refs,
    int count,
    int window_size)
{
    FILE *report;

    int total_size = 0;
    int max_size = 0;

    if (window_size < 1 ||
        window_size > MAX_REF) {

        printf(
            "PageSim: window must be between 1 and %d.\n",
            MAX_REF
        );

        return;
    }

    report = fopen("pagesim_report.txt", "a");

    if (report == NULL)
        perror("PageSim report");

    printf("\nWorking-Set Analysis\n");
    printf(
        "Window size: %d references\n",
        window_size
    );

    printf(
        "%-6s %-6s %-8s %s\n",
        "Step",
        "Page",
        "WS Size",
        "Working Set"
    );

    printf("------------------------------------------------\n");

    if (report != NULL) {
        fprintf(
            report,
            "\nWorking-Set Analysis\n"
            "Window size: %d references\n",
            window_size
        );

        fprintf(
            report,
            "%-6s %-6s %-8s %s\n",
            "Step",
            "Page",
            "WS Size",
            "Working Set"
        );
    }

    for (int step = 0;
         step < count;
         step++) {

        int start =
            step - window_size + 1;

        int working_set[MAX_REF];
        int ws_count = 0;

        if (start < 0)
            start = 0;

        /*
         * Build the distinct page set within
         * the current window.
         */
        for (int j = start; j <= step; j++) {
            int already_seen = 0;

            for (int k = 0;
                 k < ws_count;
                 k++) {

                if (working_set[k] == refs[j]) {
                    already_seen = 1;
                    break;
                }
            }

            if (!already_seen)
                working_set[ws_count++] = refs[j];
        }

        /*
         * Display the current working set.
         */
        printf(
            "%-6d %-6d %-8d {",
            step + 1,
            refs[step],
            ws_count
        );

        if (report != NULL) {
            fprintf(
                report,
                "%-6d %-6d %-8d {",
                step + 1,
                refs[step],
                ws_count
            );
        }

        for (int k = 0;
             k < ws_count;
             k++) {

            printf("%d", working_set[k]);

            if (report != NULL)
                fprintf(report, "%d", working_set[k]);

            if (k < ws_count - 1) {
                printf(", ");

                if (report != NULL)
                    fprintf(report, ", ");
            }
        }

        printf("}\n");

        if (report != NULL)
            fprintf(report, "}\n");

        total_size += ws_count;

        if (ws_count > max_size)
            max_size = ws_count;
    }

    printf("------------------------------------------------\n");

    printf(
        "Average working-set size: %.2f\n",
        (double)total_size / count
    );

    printf(
        "Maximum working-set size: %d\n",
        max_size
    );

    if (report != NULL) {
        fprintf(
            report,
            "Average working-set size: %.2f\n",
            (double)total_size / count
        );

        fprintf(
            report,
            "Maximum working-set size: %d\n\n",
            max_size
        );

        fclose(report);

        printf(
            "Working-set results appended to pagesim_report.txt\n"
        );
    }
}


/*
 * BELADY'S ANOMALY TEST
 *
 * Run FIFO with frame counts from 1 to max_frames.
 * Report if faults increase when the number of frames increases.
 */
static void run_anomaly_test(
    int max_frames,
    const int *refs,
    int count)
{
    FILE *report = fopen("pagesim_report.txt", "a");

    int previous_faults = -1;
    int found = 0;

    if (report == NULL)
        perror("PageSim report");

    printf("\nFIFO Belady Anomaly Test\n");
    printf("Frames   Faults       Hits\n");

    if (report != NULL) {
        fprintf(
            report,
            "\nFIFO Belady Anomaly Test\n"
            "Frames   Faults       Hits\n"
        );
    }

    for (int n = 1; n <= max_frames; n++) {
        int hits = 0;
        int faults = 0;

        if (!simulate_counts(
                n,
                "fifo",
                refs,
                count,
                &hits,
                &faults)) {

            printf(
                "PageSim: anomaly simulation failed.\n"
            );

            if (report != NULL)
                fclose(report);

            return;
        }

        printf(
            "%-8d %-12d %-8d\n",
            n,
            faults,
            hits
        );

        if (report != NULL) {
            fprintf(
                report,
                "%-8d %-12d %-8d\n",
                n,
                faults,
                hits
            );
        }

        if (previous_faults >= 0 &&
            faults > previous_faults) {

            printf(
                "Belady anomaly: faults increased at %d frames.\n",
                n
            );

            if (report != NULL) {
                fprintf(
                    report,
                    "Belady anomaly: faults increased at %d frames.\n",
                    n
                );
            }

            found = 1;
        }

        previous_faults = faults;
    }

    if (!found) {
        printf(
            "No anomaly observed in this frame range.\n"
        );

        if (report != NULL) {
            fprintf(
                report,
                "No anomaly observed in this frame range.\n"
            );
        }
    }

    if (report != NULL) {
        fputc('\n', report);
        fclose(report);

        printf(
            "Anomaly results appended to pagesim_report.txt\n"
        );
    }
}


/*
 * ALGORITHM COMPARISON
 *
 * Compare page hits, page faults, and hit ratio
 * for all supported replacement algorithms.
 */
static void run_comparison(
    int frame_count,
    const int *refs,
    int count)
{
    static const char *names[] = {
        "fifo",
        "lru",
        "lfu",
        "clock",
        "hybrid",
        "awsf",
        "optimal"
    };

    static const char *labels[] = {
        "FIFO",
        "LRU",
        "LFU",
        "Clock",
        "Hybrid",
        "AWSF",
        "Optimal"
    };

    int hits[7];
    int faults[7];

    int best_faults = INT_MAX;

    FILE *report =
        fopen("pagesim_report.txt", "a");

    if (report == NULL)
        perror("PageSim report");

    printf("\nPageSim Algorithm Comparison\n");

    printf(
        "Frames: %d | References: %d\n",
        frame_count,
        count
    );

    printf(
        "%-12s %7s %9s %11s\n",
        "Algorithm",
        "Hits",
        "Faults",
        "Hit Ratio"
    );

    printf("------------------------------------------\n");

    if (report != NULL) {
        fprintf(
            report,
            "\nPageSim Algorithm Comparison\n"
            "Frames: %d | References: %d\n",
            frame_count,
            count
        );

        fprintf(
            report,
            "%-12s %7s %9s %11s\n",
            "Algorithm",
            "Hits",
            "Faults",
            "Hit Ratio"
        );

        fprintf(
            report,
            "------------------------------------------\n"
        );
    }

    for (int i = 0; i < 7; i++) {
        if (!simulate_counts(
                frame_count,
                names[i],
                refs,
                count,
                &hits[i],
                &faults[i])) {

            printf(
                "PageSim: comparison failed for %s.\n",
                labels[i]
            );

            if (report != NULL)
                fclose(report);

            return;
        }

        double ratio =
            100.0 * (double)hits[i] / count;

        printf(
            "%-12s %7d %9d %10.2f%%\n",
            labels[i],
            hits[i],
            faults[i],
            ratio
        );

        if (report != NULL) {
            fprintf(
                report,
                "%-12s %7d %9d %10.2f%%\n",
                labels[i],
                hits[i],
                faults[i],
                ratio
            );
        }

        if (faults[i] < best_faults)
            best_faults = faults[i];
    }

    printf("------------------------------------------\n");
    printf("Lowest fault count: ");

    if (report != NULL) {
        fprintf(
            report,
            "------------------------------------------\n"
            "Lowest fault count: "
        );
    }

    int first = 1;

    for (int i = 0; i < 7; i++) {
        if (faults[i] == best_faults) {
            if (!first) {
                printf(", ");

                if (report != NULL)
                    fprintf(report, ", ");
            }

            printf("%s", labels[i]);

            if (report != NULL)
                fprintf(report, "%s", labels[i]);

            first = 0;
        }
    }

    printf(" (%d faults)\n", best_faults);

    if (report != NULL) {
        fprintf(
            report,
            " (%d faults)\n\n",
            best_faults
        );

        fclose(report);

        printf(
            "Report appended to pagesim_report.txt\n"
        );
    }
}


/*
 * PUBLIC ENTRY POINT
 *
 * frame_count : number of available frames
 * algo        : replacement algorithm or analysis mode
 * ref         : reference string
 * window_size : working-set window size
 */
void pagesim_run(
    int frame_count,
    const char *algo,
    const char *ref,
    int window_size)
{
    int refs[MAX_REF];
    int count = 0;

    SimulationStats stats;

    if (algo == NULL ||
        !parse_references(ref, refs, &count)) {

        return;
    }

    if (frame_count <= 0 ||
        frame_count > MAX_FRAMES) {

        printf(
            "PageSim: frames must be between 1 and %d.\n",
            MAX_FRAMES
        );

        return;
    }

    /*
     * WORKING-SET MODE
     */
    if (strcmp(algo, "working-set") == 0) {
        run_working_set(
            refs,
            count,
            window_size
        );

        return;
    }

    /*
     * BELADY'S ANOMALY MODE
     */
    if (strcmp(algo, "anomaly") == 0) {
        if (frame_count < 2) {
            printf(
                "PageSim: use at least 2 frames for anomaly mode.\n"
            );

            return;
        }

        run_anomaly_test(
            frame_count,
            refs,
            count
        );

        return;
    }

    /*
     * ALGORITHM COMPARISON MODE
     */
    if (strcmp(algo, "compare") == 0) {
        run_comparison(
            frame_count,
            refs,
            count
        );

        return;
    }

    /*
     * NORMAL REPLACEMENT SIMULATION
     */
    if (!is_supported_algorithm(algo)) {
        printf(
            "PageSim: unknown algorithm '%s'.\n",
            algo
        );

        printf(
            "Supported algorithms: fifo, lru, lfu, clock, "
            "hybrid, awsf, optimal, compare, anomaly, working-set\n"
        );

        return;
    }

    printf("Reference: %s\n", ref);

    if (!simulate(
            algo,
            frame_count,
            refs,
            count,
            1,
            &stats)) {

        printf(
            "PageSim: simulation failed.\n"
        );

        return;
    }
}
