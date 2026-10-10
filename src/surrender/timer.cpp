#include "surrender/srTimer.h"

#include <ctype.h>
#include <ostream>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <SDL3/SDL_platform.h>
#include <thread>

/* reset()'s persistence record: the registry round-trip pairs CPU identity
   with the measured tick frequency and the read hook.  calibrate() fills the
   CPUID side when the stored signature does not describe the running CPU. */
struct srTimerConfig {
    w8_long use_stored;               /* +0x00 */
    w8_long unused;                   /* +0x04 */
    w8_long save;                     /* +0x08 */
    w8_long cpuid_support;            /* +0x0c */
    w8_long cpu_count;                /* +0x10 */
    char cpu_vendor[0x10];         /* +0x14 */
    w8_ulong cpu_max_id;      /* +0x24 */
    w8_ulong cpu_signature;   /* +0x28 */
    w8_ulong cpu_features;    /* +0x2c */
    char os_ident[0x400];          /* +0x30 */
    char cpu_ident[0x400];         /* +0x430 */
    srQuadWord frequency;          /* +0x830 */
    srTimer::TickReader read_tick; /* +0x838 */
};

int calibrate(srTimerConfig* config);

namespace {
unsigned __int64 quadWord64(const srQuadWord& value)
{
    return ((unsigned __int64)value.hi << 32) | value.lo;
}

// GLOBAL: SURRENDER 0x100A49B4
char storage_class[0x1c];
} // namespace

// GLOBAL: SURRENDER 0x1009C710
unsigned short srTimer::cpuFreqVariancePct = 4;

// GLOBAL: SURRENDER 0x1009C712
short srTimer::osThreadState = -1;

// GLOBAL: SURRENDER 0x1009C714
const char* srTimer::default_storage = "hkcu:SOFTWARE/Hybrid";

// GLOBAL: SURRENDER 0x1009C718
const char* srTimer::RegRoot = "hrtimer";

// GLOBAL: SURRENDER 0x1009C71C
const char* srTimer::RegCpuFreq = "hrticks";

// GLOBAL: SURRENDER 0x1009C720
const char* srTimer::RegCpuSIG = "hrsignature";

// GLOBAL: SURRENDER 0x1009C724
const char* srTimer::RegCpuMaxID = "hrmid";

// GLOBAL: SURRENDER 0x1009C728
const char* srTimer::RegCpuVers = "hrvers";

// GLOBAL: SURRENDER 0x1009C72C
const char* srTimer::RegCpuFeatures = "hrfeat";

// GLOBAL: SURRENDER 0x1009C730
const char* srTimer::RegCpuVariance = "hrvariance";

// GLOBAL: SURRENDER 0x10077608
const w8_ulong srTimer::CPU_Model_Mask = 0x3fff;

// GLOBAL: SURRENDER 0x1007760C
const w8_ulong srTimer::CPU_Features_Mask = 0x800011;

// GLOBAL: SURRENDER 0x100A8A30
char srTimer::RegKeyName[0x400];

// GLOBAL: SURRENDER 0x100A8E30
char srTimer::osIdent[0x400];

// GLOBAL: SURRENDER 0x100A9230
void* srTimer::RegKeyBase = 0;

// FUNCTION: SURRENDER 0x100609F0
srTimer::srTimer(int force_system_timer, int unused, int save_calibration)
{
    m_frequency.lo = 0;
    m_frequency.hi = 0;
    m_base.lo = 0;
    m_base.hi = 0;
    m_tick.lo = 0;
    m_tick.hi = 0;
    m_pause.lo = 0;
    m_pause.hi = 0;
    m_kernel32 = 0;
    m_read_tick = 0;
    setUnits(1000);
    reset(force_system_timer, unused, save_calibration);
}

/* The copy omits both conversion scales and the CPU count, and reloads kernel32. */
// FUNCTION: SURRENDER 0x10060B90
srTimer::srTimer(const srTimer& other)
{
    int index;
    for (index = 0; index < 0x400; ++index) {
        m_ident[index] = other.m_ident[index];
    }
    for (index = 0; index < 0x400; ++index) {
        m_cpu_ident[index] = other.m_cpu_ident[index];
    }
    m_frequency = other.m_frequency;
    m_base = other.m_base;
    m_tick = other.m_tick;
    m_pause = other.m_pause;
    m_units_per_interval = other.m_units_per_interval;
    m_read_tick = other.m_read_tick;
    m_kernel32 = 0;
    for (index = 0; index < 0xd; ++index) {
        m_cpu_vendor[index] = other.m_cpu_vendor[index];
    }
    m_cpu_max_id = other.m_cpu_max_id;
    m_cpu_signature = other.m_cpu_signature;
    m_cpu_features = other.m_cpu_features;
    m_pause.lo = 0;
    m_pause.hi = 0;
}

/* Assignment omits the same fields and reloads kernel32 without releasing any prior handle. */
// FUNCTION: SURRENDER 0x10062480
srTimer& srTimer::operator=(const srTimer& other)
{
    int index;
    for (index = 0; index < 0x400; ++index) {
        m_ident[index] = other.m_ident[index];
    }
    for (index = 0; index < 0x400; ++index) {
        m_cpu_ident[index] = other.m_cpu_ident[index];
    }
    m_frequency = other.m_frequency;
    m_base = other.m_base;
    m_tick = other.m_tick;
    m_pause = other.m_pause;
    m_units_per_interval = other.m_units_per_interval;
    m_read_tick = other.m_read_tick;
    m_kernel32 = 0;
    for (index = 0; index < 0xd; ++index) {
        m_cpu_vendor[index] = other.m_cpu_vendor[index];
    }
    m_cpu_max_id = other.m_cpu_max_id;
    m_cpu_signature = other.m_cpu_signature;
    m_cpu_features = other.m_cpu_features;
    return *this;
}

// FUNCTION: SURRENDER 0x10060F80
srTimer::~srTimer() {}

/* Native ticks are microseconds of the monotonic clock. */
int __stdcall srTimer::getTick(srQuadWord* out)
{
    *out = (unsigned __int64)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
               .count();
    return 1;
}

int __stdcall srTimer::RDTSC(srQuadWord* out)
{
    return getTick(out);
}

int srTimer::getCPUIDSupport() const
{
    return 0;
}

int srTimer::reset(int, int, int)
{
    m_cpu_count = std::thread::hardware_concurrency();
    if (m_cpu_count)
        snprintf(m_cpu_ident, sizeof(m_cpu_ident), "%d logical CPUs", m_cpu_count);
    else
        strcpy(m_cpu_ident, "unknown CPU count");
    memset(m_cpu_vendor, 0, sizeof(m_cpu_vendor));
    m_cpu_max_id = 0;
    m_cpu_signature = 0;
    m_cpu_features = 0;
    m_read_tick = getTick;
    m_frequency.lo = 1000000;
    m_frequency.hi = 0;
    m_seconds_per_tick = 1.0 / m_frequency;
    m_units_per_tick = m_units_per_interval * m_seconds_per_tick;
    strcpy(m_ident, "std::chrono::steady_clock");
    return m_read_tick(&m_base);
}

// FUNCTION: SURRENDER 0x10062230
void srTimer::getFreq(srQuadWord& out) const
{
    out.lo = m_frequency.lo;
    out.hi = m_frequency.hi;
}

// FUNCTION: SURRENDER 0x10062250
w8_ulong srTimer::getUnits() const
{
    return m_units_per_interval;
}

// FUNCTION: SURRENDER 0x10062260
void srTimer::setUnits(w8_ulong units)
{
    m_units_per_interval = units;
    if (m_frequency == 0.0) {
        m_units_per_tick = units / 1.0;
    } else {
        m_units_per_tick = units / (double)m_frequency;
    }
}

// FUNCTION: SURRENDER 0x10062320
const char* srTimer::getIdent() const
{
    return m_ident;
}

// FUNCTION: SURRENDER 0x10062350
srTimer::e_cpuTypeId srTimer::getCPUType() const
{
    return (e_cpuTypeId)(m_cpu_signature >> 0xc & 3);
}

// FUNCTION: SURRENDER 0x10062360
unsigned short srTimer::getCPUFamily() const
{
    return (unsigned short)(m_cpu_signature >> 8) & 0xf;
}

// FUNCTION: SURRENDER 0x10062370
unsigned short srTimer::getCPUModel() const
{
    return (unsigned short)(m_cpu_signature >> 4) & 0xf;
}

// FUNCTION: SURRENDER 0x10062380
unsigned short srTimer::getCPUStepping() const
{
    return (unsigned short)(m_cpu_signature & 0xf);
}

// FUNCTION: SURRENDER 0x10062390
int srTimer::getFeature(w8_long feature) const
{
    return (m_cpu_features & (1 << feature)) != 0;
}

// FUNCTION: SURRENDER 0x100623E0
w8_ulong srTimer::getRawTime(e_timerReadControl control)
{
    if (control == TIMER_READ_DEFAULT) {
        m_read_tick(&m_tick);
    }
    return m_tick.lo - m_base.lo;
}

// FUNCTION: SURRENDER 0x10062430
w8_ulong srTimer::getRawTime(srQuadWord& out, e_timerReadControl control)
{
    if (control == TIMER_READ_DEFAULT) {
        m_read_tick(&m_tick);
    }
    out = m_tick - m_base;
    return out.lo;
}

// FUNCTION: SURRENDER 0x100625A0
char* srTimer::getStorage(char* buffer, w8_ulong size)
{
    memset(buffer, 0, size);
    if (RegKeyBase == (void*)0x80000001) {
        strcpy(buffer, "hkcu");
    } else if (RegKeyBase == (void*)0x80000002) {
        strcpy(buffer, "hklm");
    } else if (RegKeyBase == (void*)0x80000005) {
        strcpy(buffer, "hkcc");
    } else if (RegKeyBase == (void*)0x80000000) {
        strcpy(buffer, "hkcr");
    } else if (RegKeyBase == (void*)0x80000003) {
        strcpy(buffer, "hkus");
    } else if (RegKeyBase == (void*)0x80000004) {
        strcpy(buffer, "hkpd");
    } else {
        sprintf(buffer, "0x%08X", (unsigned int)(size_t)RegKeyBase);
    }
    strcat(buffer, ":");
    strncat(buffer, RegKeyName, size - 5);
    char* slash = strchr(buffer, '\\');
    while (slash != 0) {
        *slash = '/';
        slash = strchr(buffer, '\\');
    }
    return buffer;
}

// FUNCTION: SURRENDER 0x10062770
int srTimer::isPaused() const
{
    if ((m_pause.lo | m_pause.hi) != 0) {
        return 1;
    }
    return 0;
}

/* The calibration cache lived in the Windows registry; the native clock needs none. */
int srTimer::store()
{
    return 0;
}

int srTimer::retrieve()
{
    return 0;
}

// FUNCTION: SURRENDER 0x10062D20
int srTimer::pause()
{
    if ((m_pause.lo | m_pause.hi) != 0) {
        return 0;
    }
    m_read_tick(&m_pause);
    return 1;
}

// FUNCTION: SURRENDER 0x10062D50
w8_ulong srTimer::resume()
{
    srQuadWord delta = {0, 0};
    if ((m_pause.lo | m_pause.hi) == 0) {
        srQuadWord now;
        m_read_tick(&now);
        delta = now - m_pause;
        m_base += delta;
        m_pause.lo = 0;
        m_pause.hi = 0;
    }
    return ((delta * m_units_per_interval) / m_frequency).lo;
}

// FUNCTION: SURRENDER 0x10062DF0
w8_ulong srTimer::getMsTime(e_timerReadControl control)
{
    getUTime(control);
    return (((m_tick - m_base) * 1000u) / m_frequency).lo;
}

// FUNCTION: SURRENDER 0x10062E50
double srTimer::getTime(e_timerReadControl control)
{
    if (control == TIMER_READ_DEFAULT) {
        m_read_tick(&m_tick);
    }
    return (m_tick - m_base) * m_seconds_per_tick;
}

// FUNCTION: SURRENDER 0x10062EC0
w8_ulong srTimer::getUTime(e_timerReadControl control)
{
    if (control == TIMER_READ_DEFAULT) {
        m_read_tick(&m_tick);
    }
    return (w8_ulong)((m_tick - m_base) * m_units_per_tick);
}

// FUNCTION: SURRENDER 0x10062F40
w8_ulong srTimer::getUTime(srQuadWord& out, e_timerReadControl control)
{
    if (control == TIMER_READ_DEFAULT) {
        m_read_tick(&m_tick);
    }
    unsigned __int64 units = (unsigned __int64)((m_tick - m_base) * m_units_per_tick);
    out = units;
    return out.lo;
}

// FUNCTION: SURRENDER 0x10062FC0
char* srTimer::getAscTime(char* buffer, e_timerReadControl control)
{
    getUTime(control);
    srQuadWord ticks = ((m_tick - m_base) * m_units_per_interval) / m_frequency;
    return getAscTime(buffer, ticks);
}

/* Writes "HHH:MM:SS.mmm"; the hour field is 3-wide until it overflows into
   "###" and the seconds carry the fraction. */
// FUNCTION: SURRENDER 0x10063030
char* srTimer::getAscTime(char* buffer, srQuadWord ticks)
{
    *buffer = '\0';
    float seconds = (float)((ticks.lo + ticks.hi * 4294967296.0) / m_units_per_interval);
    if (seconds >= 3600.0f) {
        w8_long hours = (w8_long)(seconds / 3600.0f);
        seconds -= (float)(hours * 0xe10);
        if (hours < 1000) {
            sprintf(buffer + strlen(buffer), "%03lu:", (unsigned long)hours);
        } else {
            strcat(buffer, "###:");
        }
    } else {
        strcat(buffer, "000:");
    }
    if (seconds >= 60.0f) {
        w8_long minutes = (w8_long)(seconds / 60.0f);
        seconds -= (float)(minutes * 0x3c);
        sprintf(buffer + strlen(buffer), "%02lu:", (unsigned long)minutes);
    } else {
        strcat(buffer, "00:");
    }
    sprintf(buffer + strlen(buffer), "%06.3f", seconds);
    return buffer;
}

// FUNCTION: SURRENDER 0x10063200
const char* srTimer::getCPUTypeIdString(e_cpuTypeId type) const
{
    const char* names[] = {"OEM", "Overdrive", "SMP", "Unknown"};
    if (4 <= (unsigned int)type) {
        type = CPU_TYPE_RESERVED;
    }
    return names[type];
}

// FUNCTION: SURRENDER 0x10062790
void srTimer::setStorage(char* const storage)
{
    char* value = storage;
    if (value == 0) {
        value = (char*)default_storage;
    }
    char* colon = strchr(value, ':');
    if (colon == 0 || (unsigned short)(colon - value) > 10) {
        return;
    }
    char root[11];
    memset(root, 0, sizeof(root));
    strncpy(root, value, colon - value);
    if (_strnicmp(root, "hkcu", 4) == 0) {
        value = (char*)0x80000001;
    } else if (_strnicmp(root, "hklm", 4) == 0) {
        value = (char*)0x80000002;
    } else if (_strnicmp(root, "hkcr", 4) == 0) {
        value = (char*)0x80000000;
    } else if (_strnicmp(root, "hkus", 4) == 0) {
        value = (char*)0x80000003;
    } else if (_strnicmp(root, "hkpd", 4) == 0) {
        value = (char*)0x80000004;
    } else if (_strnicmp(root, "hkcc", 4) == 0) {
        value = (char*)0x80000005;
    } else {
        if (_strnicmp(root, "0x", 2) != 0 || strlen(root) != 0xa) {
            return;
        }
        sscanf("%d", root, &value);
        if (value == 0) {
            return;
        }
    }
    RegKeyBase = value;
    strcpy(RegKeyName, colon + 1);
    char* slash = strchr(RegKeyName, '/');
    while (slash != 0) {
        *slash = '\\';
        slash = strchr(RegKeyName, '/');
    }
}

// FUNCTION: SURRENDER 0x10060CC0
const char* srTimer::getOsIdent() const
{
    if (osThreadState != -1) {
        return osIdent;
    }
    osThreadState = 1;
    snprintf(osIdent, sizeof(osIdent), "%s", SDL_GetPlatform());
    return osIdent;
}

/* The stream's width field doubles as the print-mode selector: 1 prints the
   CPU identity and frequency, anything else the OS identity; the mode is
   restored afterwards. */
// FUNCTION: SURRENDER 0x10060F90
std::ostream& operator<<(std::ostream& stream, const srTimer& timer)
{
    int mode = stream.width();
    if (mode == 1) {
        stream << timer.m_cpu_ident << " @ " << timer.m_frequency * 1e-6 << " Mhz";
    } else {
        stream << timer.m_ident;
    }
    stream.width(mode);
    return stream;
}
