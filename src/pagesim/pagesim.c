#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pagesim.h"

#define MAX_REF 256
#define MAX_FRAMES 64

static int find_page(int *frames, int n, int page)
{
    for (int i = 0; i < n; i++)
        if (frames[i] == page)
            return i;
    return -1;
}

static void print_frames(int *frames, int n)
{
    printf("[");
    for (int i = 0; i < n; i++) {
        if (frames[i] == -1) printf("-");
        else printf("%d", frames[i]);

        if (i < n - 1) printf(" ");
    }
    printf("]");
}


static int simulate_counts(
    int n, const char *algo, const int *refs, int count,
    int *hits_out, int *faults_out)
{
    int f[MAX_FRAMES], age[MAX_FRAMES];
    int freq[MAX_FRAMES], arrival[MAX_FRAMES], refbit[MAX_FRAMES];
    int hits = 0, faults = 0, fifo_pos = 0, clock_hand = 0;

    for (int i = 0; i < n; i++) {
        f[i] = -1;
        age[i] = 0;
        freq[i] = 0;
        arrival[i] = -1;
        refbit[i] = 0;
    }

    for (int step = 0; step < count; step++) {
        int page = refs[step];
        int pos = find_page(f, n, page);

        if (pos >= 0) {
            hits++;
            freq[pos]++;
            age[pos] = step;
            refbit[pos] = 1;
            continue;
        }

        faults++;
        int v = -1;

        for (int i = 0; i < n; i++) {
            if (f[i] == -1) {
                v = i;
                break;
            }
        }

        if (v == -1) {
            if (strcmp(algo, "fifo") == 0) {
                v = fifo_pos;
                fifo_pos = (fifo_pos + 1) % n;
            } else if (strcmp(algo, "clock") == 0) {
                while (refbit[clock_hand]) {
                    refbit[clock_hand] = 0;
                    clock_hand = (clock_hand + 1) % n;
                }
                v = clock_hand;
                clock_hand = (clock_hand + 1) % n;
            } else if (strcmp(algo, "lru") == 0) {
                v = 0;
                for (int i = 1; i < n; i++)
                    if (age[i] < age[v]) v = i;
            } else if (strcmp(algo, "lfu") == 0) {
                v = 0;
                for (int i = 1; i < n; i++)
                    if (freq[i] < freq[v] ||
                        (freq[i] == freq[v] && age[i] < age[v]))
                        v = i;
            } else if (strcmp(algo, "hybrid") == 0) {
                int min_freq = freq[0];
                for (int i = 1; i < n; i++)
                    if (freq[i] < min_freq) min_freq = freq[i];

                int oldest_group = -1;
                for (int i = 0; i < n; i++) {
                    if (freq[i] == min_freq) {
                        int group = (step - age[i]) / 3;
                        if (group > oldest_group) oldest_group = group;
                    }
                }

                v = -1;
                for (int i = 0; i < n; i++) {
                    if (freq[i] == min_freq &&
                        (step - age[i]) / 3 == oldest_group &&
                        (v == -1 || arrival[i] < arrival[v]))
                        v = i;
                }
            } else if (strcmp(algo, "optimal") == 0) {
                int farthest = -1;
                for (int i = 0; i < n; i++) {
                    int next = count + 1;
                    for (int j = step + 1; j < count; j++) {
                        if (refs[j] == f[i]) {
                            next = j;
                            break;
                        }
                    }
                    if (next > farthest) {
                        farthest = next;
                        v = i;
                    }
                }
            } else {
                return 0;
            }
        }

        f[v] = page;
        freq[v] = 1;
        age[v] = step;
        arrival[v] = step;
        refbit[v] = 1;
    }

    *hits_out = hits;
    *faults_out = faults;
    return 1;
}

void pagesim_run(int frame_count, const char *algo, const char *ref)
{
    int refs[MAX_REF];
    int frames[MAX_FRAMES];
    int age[MAX_FRAMES];
int freq[MAX_FRAMES];
int arrival[MAX_FRAMES];
    int refbit[MAX_FRAMES];

    int count = 0;
    int hits = 0;
    int faults = 0;
    int fifo_pos = 0;
    int clock_hand = 0;

    char buffer[1024];
    strncpy(buffer, ref, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *tok = strtok(buffer, " ,\t\n");

    while (tok && count < MAX_REF) {
        refs[count++] = atoi(tok);
        tok = strtok(NULL, " ,\t\n");
    }

    if (frame_count <= 0 || frame_count > MAX_FRAMES || count == 0) {
        printf("PageSim: invalid frames or reference string.\n");
        return;
    }



    if (strcmp(algo, "anomaly") == 0) {
        if (frame_count < 2) {
            printf("Use at least 2 frames for anomaly mode.\n");
            return;
        }

        FILE *report = fopen("pagesim_report.txt", "a");
        if (report == NULL)
            perror("PageSim report");

        int previous = -1;
        int found = 0;

        printf("\nFIFO Belady Anomaly Test\n");
        printf("Reference: %s\n", ref);
        printf("%-8s %-12s %-8s\n", "Frames", "Faults", "Hits");

        if (report != NULL) {
            fprintf(report, "\nFIFO Belady Anomaly Test\n");
            fprintf(report, "Reference: %s\n", ref);
            fprintf(report, "%-8s %-12s %-8s\n",
                    "Frames", "Faults", "Hits");
        }

        for (int n = 1; n <= frame_count; n++) {
            int h = 0, f = 0;

            if (!simulate_counts(n, "fifo", refs, count, &h, &f)) {
                printf("Anomaly test failed.\n");
                if (report != NULL)
                    fclose(report);
                return;
            }

            printf("%-8d %-12d %-8d\n", n, f, h);

            if (report != NULL)
                fprintf(report, "%-8d %-12d %-8d\n", n, f, h);

            if (previous >= 0 && f > previous) {
                printf("Belady anomaly: faults increased at %d frames.\n", n);
                if (report != NULL)
                    fprintf(report,
                            "Belady anomaly: faults increased at %d frames.\n",
                            n);
                found = 1;
            }

            previous = f;
        }

        if (!found) {
            printf("No anomaly observed in this frame range.\n");
            if (report != NULL)
                fprintf(report, "No anomaly observed in this frame range.\n");
        }

        if (report != NULL) {
            fprintf(report, "\n");
            fclose(report);
            printf("Anomaly results appended to pagesim_report.txt\n");
        }

        return;
    }

    if (strcmp(algo, "compare") == 0) {
        const char *names[] = {
            "fifo", "lru", "lfu", "clock", "hybrid", "optimal"
        };
        const char *labels[] = {
            "FIFO", "LRU", "LFU", "Clock", "Hybrid", "Optimal"
        };
        int hits[6], faults[6], best = MAX_REF + 1;

        FILE *report = fopen("pagesim_report.txt", "a");
        if (report == NULL)
            perror("PageSim report");

        printf("\nPageSim Algorithm Comparison\n");
        printf("Frames: %d | References: %d\n", frame_count, count);
        printf("%-12s %7s %9s %11s\n",
               "Algorithm", "Hits", "Faults", "Hit Ratio");
        printf("------------------------------------------\n");

        if (report != NULL) {
            fprintf(report, "\nPageSim Algorithm Comparison\n");
            fprintf(report, "Frames: %d | References: %d\n",
                    frame_count, count);
            fprintf(report, "%-12s %7s %9s %11s\n",
                    "Algorithm", "Hits", "Faults", "Hit Ratio");
            fprintf(report, "------------------------------------------\n");
        }

        for (int i = 0; i < 6; i++) {
            if (!simulate_counts(frame_count, names[i], refs, count,
                                 &hits[i], &faults[i])) {
                printf("Comparison failed for %s.\n", labels[i]);
                return;
            }

            printf("%-12s %7d %9d %10.2f%%\n",
                   labels[i], hits[i], faults[i],
                   (double)hits[i] / count * 100.0);

            if (report != NULL) {
                fprintf(report, "%-12s %7d %9d %10.2f%%\n",
                        labels[i], hits[i], faults[i],
                        (double)hits[i] / count * 100.0);
            }

            if (faults[i] < best) best = faults[i];
        }

        printf("------------------------------------------\n");
        printf("Lowest fault count: ");
        int first = 1;
        for (int i = 0; i < 6; i++) {
            if (faults[i] == best) {
                printf("%s%s", first ? "" : ", ", labels[i]);
                first = 0;
            }
        }
        printf(" (%d faults)\n\n", best);

        if (report != NULL) {
            fprintf(report, "Lowest fault count: ");
            int report_first = 1;

            for (int i = 0; i < 6; i++) {
                if (faults[i] == best) {
                    fprintf(report, "%s%s",
                            report_first ? "" : ", ", labels[i]);
                    report_first = 0;
                }
            }

            fprintf(report, " (%d faults)\n\n", best);
            fclose(report);
            printf("Report appended to pagesim_report.txt\n");
        }

        return;
    }

    for (int i = 0; i < frame_count; i++) {
        frames[i] = -1;
        age[i] = 0;
        freq[i] = 0;
        arrival[i] = -1;
        refbit[i] = 0;
    }

    printf("\n========================================\n");
    printf("           ShellForge PageSim\n");
    printf("========================================\n");
    printf("Algorithm : %s\n", algo);
    printf("Frames    : %d\n", frame_count);
    printf("Reference : %s\n\n", ref);

    printf("%-5s %-5s %-18s %s\n", "Step", "Page", "Frames", "Result");
    printf("----------------------------------------\n");

    for (int step = 0; step < count; step++) {

        int page = refs[step];
        int pos = find_page(frames, frame_count, page);

        if (pos != -1) {
            hits++;
            freq[pos]++;
            age[pos] = step;
            refbit[pos] = 1;

            printf("%-5d %-5d ", step + 1, page);
            print_frames(frames, frame_count);
            printf("     HIT\n");
            continue;
        }

        faults++;

        int victim = -1;

        /* Empty frame first */
        for (int i = 0; i < frame_count; i++) {
            if (frames[i] == -1) {
                victim = i;
                break;
            }
        }

        /* Replacement */
        if (victim == -1) {

            if (strcmp(algo, "fifo") == 0) {
                victim = fifo_pos;
                fifo_pos = (fifo_pos + 1) % frame_count;
            }

            else if (strcmp(algo, "clock") == 0) {
                while (refbit[clock_hand]) {
                    refbit[clock_hand] = 0;
                    clock_hand = (clock_hand + 1) % frame_count;
                }
                victim = clock_hand;
                clock_hand = (clock_hand + 1) % frame_count;
            }

            else if (strcmp(algo, "lru") == 0) {
                victim = 0;

                for (int i = 1; i < frame_count; i++)
                    if (age[i] < age[victim])
                        victim = i;
            }

            else if (strcmp(algo, "lfu") == 0) {
                victim = 0;

                for (int i = 1; i < frame_count; i++) {
                    if (freq[i] < freq[victim] ||
                       (freq[i] == freq[victim] &&
                        age[i] < age[victim])) {
                        victim = i;
                    }
                }
            }
else if (strcmp(algo, "hybrid") == 0) {

    /*
     * HYBRID PAGE REPLACEMENT
     *
     * Stage 1: LFU
     * Find the minimum access frequency.
     */
    int min_freq = freq[0];

    for (int i = 1; i < frame_count; i++) {
        if (freq[i] < min_freq)
            min_freq = freq[i];
    }

    /*
     * Stage 2: LRU
     * Among LFU candidates, find the oldest
     * recency group. One group spans 3 accesses.
     */
    int oldest_group = -1;

    for (int i = 0; i < frame_count; i++) {
        if (freq[i] == min_freq) {

            int stale_group = (step - age[i]) / 3;

            if (stale_group > oldest_group)
                oldest_group = stale_group;
        }
    }

    /*
     * Stage 3: FIFO
     * Among candidates in that recency group,
     * choose the page that arrived first.
     */
    victim = -1;

    for (int i = 0; i < frame_count; i++) {
        if (freq[i] == min_freq &&
            (step - age[i]) / 3 == oldest_group) {

            if (victim == -1 ||
                arrival[i] < arrival[victim]) {
                victim = i;
            }
        }
    }
}

            else if (strcmp(algo, "optimal") == 0) {

                int farthest = -1;

                for (int i = 0; i < frame_count; i++) {

                    int next = count + 1;

                    for (int j = step + 1; j < count; j++) {
                        if (refs[j] == frames[i]) {
                            next = j;
                            break;
                        }
                    }

                    if (next > farthest) {
                        farthest = next;
                        victim = i;
                    }
                }
            }

            else {
                printf("Unknown algorithm: %s\n", algo);
                return;
            }
        }

        frames[victim] = page;
freq[victim] = 1;
age[victim] = step;
arrival[victim] = step;
        refbit[victim] = 1;

        printf("%-5d %-5d ", step + 1, page);
        print_frames(frames, frame_count);
        printf("     FAULT\n");
    }

    double hit_ratio = (double)hits / count * 100.0;

    printf("----------------------------------------\n");
    printf("Page Hits   : %d\n", hits);
    printf("Page Faults : %d\n", faults);
    printf("Hit Ratio   : %.2f%%\n", hit_ratio);
    printf("========================================\n\n");
}
