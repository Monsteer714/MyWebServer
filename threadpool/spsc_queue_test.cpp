//
// Created by Hanhong Wong on 2026/5/17.
//

#include "spsc_queue.h"

#include <cassert>
#include <iostream>

using namespace std;


void test_init () {
    spsc_queue<int, 5> q;
    assert(q.size() == 0);
    assert(q.empty() == true);
    assert(q.full() == false);
    assert(q.pop() == false);
}

void test_push_one_ele () {
    spsc_queue<int, 5> q;

    q.push(1);
    assert(q.size() == 1);
    assert(q.empty() == false);
    assert(q.full() == false);
    assert(q.front() == 1);
}

void test_full () {
    spsc_queue<int, 5> q;

    for (int i = 0; i < 5; i++) {
        q.push(i);
        assert(q.size() == i + 1);
    }

    assert(q.full() == true);
    assert(q.empty() == false);
    assert(q.size() == 5);
}

void test_push_after_full () {
    spsc_queue<int, 5> q;

    for (int i = 0; i < 5; i++) {
        q.push(i);
        assert(q.size() == i + 1);
    }

    assert(q.full() == true);
    assert(q.size() == 5);
    assert(q.front() == 0);

    assert(q.push(0) == false);
    assert(q.full() == true);
    assert(q.size() == 5);
    assert(q.front() == 0);
}

void test_clear_after_full () {
    spsc_queue<int, 5> q;

    for (int i = 0; i < 5; i++) {
        q.push(i);
        assert(q.size() == i + 1);
    }

    for (int i = 0; i < 5; i++) {
        assert(q.front() == i);
        assert(q.pop() == true);
    }

    assert(q.empty() == true);
    assert(q.size() == 0);
    assert(q.full() == false);
    assert(q.pop() == false);
}

void test_mega_queue() {
    const int MEGA = 1000000;
    spsc_queue<int, MEGA> q;
    assert(q.size() == 0);
    assert(q.empty() == true);
    assert(q.full() == false);
    assert(q.pop() == false);

    for (int i = 0; i < MEGA; i++) {
        q.push(i);
        assert(q.size() == i + 1);
        assert(q.front() == 0);
    }

    assert(q.full() == true);
}


void test_all() {
    test_init();
    test_mega_queue();
    test_push_one_ele();
    test_full();
    test_push_after_full();
    test_clear_after_full();
}

int main() {
    test_all();
}