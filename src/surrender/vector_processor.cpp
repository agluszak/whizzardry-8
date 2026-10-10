#include "surrender/srVectorProcessor.h"

#include <ostream>
#include <stdio.h>

#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "surrender/srDebugVP.h"
#include "surrender/srVP_generic.h"

srVP* srVectorProcessor::vp = 0;
srVP* srVectorProcessor::base = 0;
srDebugVP* srVectorProcessor::debug = 0;
w8_ulong srVectorProcessor::debug_active = 0;

// FUNCTION: SURRENDER 0x10064370
const char* srVectorProcessor::getName()
{
    if (vp == 0) {
        return 0;
    }
    return vp->getName();
}

// FUNCTION: SURRENDER 0x10064390
void srVectorProcessor::install(srVP* processor)
{
    if (vp != 0) {
        release();
    }
    vp = processor;
    base = processor;
    debug = 0;
    debug_active = 0;
}

// FUNCTION: SURRENDER 0x100643D0
void srVectorProcessor::startDebug(int check_misalignments)
{
    if (debug_active == 0) {
        debug = new srDebugVP(vp);
        vp = debug;
        debug_active = 1;
        debug->check_misalignments = check_misalignments;
    }
}

// FUNCTION: SURRENDER 0x10064450
void srVectorProcessor::endDebug()
{
    if (debug_active != 0) {
        vp = base;
        debug_active = 0;
        if (debug != 0) {
            delete debug;
        }
        debug = 0;
    }
}

// FUNCTION: SURRENDER 0x10064490
void srVectorProcessor::dump(std::ostream& stream)
{
    int command;
    int index;
    int entry;

    if (debug_active == 0) {
        srStreamPrintf(stream, "VP statistics only available with srDebugVP, use "
                               "srVectorProcessor::startDebug()\n");
        return;
    }
    srStreamPrintf(stream, "\n\n");
    srStreamPrintf(stream,
                   "------------------ srDebugVP statistics ---------------------------\n\n");
    srStreamPrintf(stream, "[%% of VP][cycles][calls][av elements][mis 8/16]\n\n");
    double frequency = srCore.getTimer()->m_frequency;
    double total = 0.0;
    int used = 0;
    for (command = 0; command < 0xa6; ++command) {
        if (debug->call_counts[command] != 0) {
            double time =
                debug->call_times[command] - debug->call_counts[command] * debug->call_overhead;
            if (time <= 0.0) {
                time = 0.0;
            }
            total += time;
            ++used;
        }
    }
    if (used == 0 || total == 0.0) {
        return;
    }
    SRDWORD* scores = static_cast<SRDWORD*>(operator new(used * sizeof(*scores)));
    int* order = static_cast<int*>(operator new(used * sizeof(*order)));
    index = 0;
    for (command = 0; command < 0xa6; ++command) {
        if (debug->call_counts[command] != 0) {
            double time =
                debug->call_times[command] - debug->call_counts[command] * debug->call_overhead;
            if (time <= 0.0) {
                time = 0.0;
            }
            scores[index] = -1 - static_cast<int>(time / total * 4294967295.0);
            order[index] = command;
            ++index;
        }
    }
    for (entry = 1; entry < used; ++entry) {
        SRDWORD score = scores[entry];
        int selected = order[entry];
        index = entry - 1;
        while (index >= 0 && scores[index] > score) {
            scores[index + 1] = scores[index];
            order[index + 1] = order[index];
            --index;
        }
        scores[index + 1] = score;
        order[index + 1] = selected;
    }
    for (index = 0; index < used; ++index) {
        command = order[index];
        double calls = debug->call_counts[command];
        double elements = debug->element_counts[command];
        double time = debug->call_times[command] - calls * debug->call_overhead;
        if (time <= 0.0) {
            time = 0.0;
        }
        char percent[0x40];
        char cycles[0x40];
        char call_text[0x40];
        char element_text[0x40];
        char misalignments[0x40];
        sprintf(percent, "%.2f%%", time * 100.0 / total);
        sprintf(cycles, "%.2f", time * frequency / elements);
        sprintf(call_text, "%d", static_cast<int>(calls));
        sprintf(element_text, "%d", static_cast<int>(elements / calls));
        sprintf(misalignments, "%d/%d", static_cast<int>(debug->misaligned8[command]),
                static_cast<int>(debug->misaligned16[command]));
        srStreamPrintf(stream, "%-8s %-8s %-8s %-9s %-12s %s\n", percent, cycles, call_text,
                       element_text, misalignments, srDebugVP::command_names[command]);
    }
    srStreamPrintf(stream, "\n\n");
    if (debug->check_misalignments == 0) {
        srStreamPrintf(stream, "misAlignments not checked\n");
    }
    srStreamPrintf(stream, "\n");
    stream << std::endl;
    operator delete(order);
    operator delete(scores);
}

// FUNCTION: SURRENDER 0x10064900
void srVectorProcessor::resetStatistics()
{
    if (debug_active != 0 && debug != 0) {
        debug->resetInternalStatistics();
    }
}

// FUNCTION: SURRENDER 0x100649C0
void srVectorProcessor::initBaseVP()
{
    install(new srVP_generic);
}

// FUNCTION: SURRENDER 0x10064BB0
void srVectorProcessor::release()
{
    if (vp != 0) {
        delete base;
        delete debug;
        vp = 0;
        base = 0;
        debug = 0;
    }
}
