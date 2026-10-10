#pragma once

/* Single-array sibling of the QuickSortByKey idiom: partition while the range
   is wide, then finish with an insertion pass. Call sites inline the outer
   frame, so the Octree.cpp emission only ever serves the recursion. */
inline void InsertionSort(w8_ulong* values, int first, int last)
{
    for (int index = first + 1; index < last; ++index) {
        w8_ulong value = values[index];
        int position = index;
        while (value < values[position - 1]) {
            values[position] = values[position - 1];
            --position;
            if (position == first) {
                break;
            }
        }
        values[position] = value;
    }
}

// FUNCTION: WIZ8 0x00438e60
inline void QuickSort(w8_ulong* values, int first, int last)
{
    if (last - first <= 8) {
        InsertionSort(values, first, last + 1);
        return;
    }
    w8_ulong pivot = values[last];
    int low = first - 1;
    int high = last;
    w8_ulong value;
    do {
        do {
            ++low;
        } while (low < last && values[low] < pivot);
        do {
            --high;
        } while (0 < high && pivot < values[high]);
        value = values[low];
        values[low] = values[high];
        values[high] = value;
    } while (low < high);
    values[high] = values[low];
    values[low] = values[last];
    values[last] = value;
    if (first < low - 1) {
        QuickSort(values, first, low - 1);
    }
    if (low + 1 < last) {
        QuickSort(values, low + 1, last);
    }
}

/* The paired-array sibling of the sorts above: items and their unsigned long
   keys move together.  Retail emits these instantiations in several TUs; the
   insertion pass only appears inside QuickSortByKey's tail or a SortByKey
   inline. */
template <class T> void InsertionSortByKey(T* items, w8_ulong* keys, int first, int last)
{
    for (int index = first + 1; index < last; ++index) {
        T item = items[index];
        w8_ulong key = keys[index];
        int position = index;
        while (key < keys[position - 1]) {
            keys[position] = keys[position - 1];
            items[position] = items[position - 1];
            --position;
            if (position == first) {
                break;
            }
        }
        keys[position] = key;
        items[position] = item;
    }
}

template <class T> void QuickSortByKey(T* items, w8_ulong* keys, int first, int last)
{
    if (last - first > 8) {
        w8_ulong pivot = keys[last];
        int low = first - 1;
        int high = last;
        T item;
        w8_ulong key;
        do {
            do {
                ++low;
            } while (low < last && keys[low] < pivot);
            do {
                --high;
            } while (high > 0 && pivot < keys[high]);
            item = items[low];
            items[low] = items[high];
            items[high] = item;
            key = keys[low];
            keys[low] = keys[high];
            keys[high] = key;
        } while (low < high);
        items[high] = items[low];
        items[low] = items[last];
        items[last] = item;
        keys[high] = keys[low];
        keys[low] = keys[last];
        keys[last] = key;
        if (first < low - 1) {
            QuickSortByKey(items, keys, first, low - 1);
        }
        if (low + 1 < last) {
            QuickSortByKey(items, keys, low + 1, last);
        }
    } else {
        InsertionSortByKey(items, keys, first, last + 1);
    }
}

template <class T> void SortByKey(T* items, w8_ulong* keys, int count)
{
    if (count > 1) {
        int ordered_pairs = 0;
        for (int index = 0; index < count - 1; ++index) {
            if (keys[index] <= keys[index + 1]) {
                ++ordered_pairs;
            }
        }
        if (ordered_pairs + 1 == count) {
            return;
        }
        if (ordered_pairs < count / 3) {
            for (int index = 0; index < count / 2; ++index) {
                T item = items[index];
                items[index] = items[count - 1 - index];
                items[count - 1 - index] = item;
                w8_ulong key = keys[index];
                keys[index] = keys[count - 1 - index];
                keys[count - 1 - index] = key;
            }
            if (ordered_pairs == 0) {
                return;
            }
            InsertionSortByKey(items, keys, 0, count);
        } else if (ordered_pairs < 50) {
            InsertionSortByKey(items, keys, 0, count);
        } else {
            QuickSortByKey(items, keys, 0, count - 1);
        }
    }
}

