//
// Simple test for sort_timer_lst
//
#include "timer_lst.h"
#include <iostream>
#include <cassert>
#include <vector>

// 简单计数器，用来验证回调是否被调用
static int cb_count = 0;
static std::vector<int> cb_order;

void test_cb(client_data* data) {
    cb_count++;
    cb_order.push_back(data->m_sock_fd_);
    std::cout << "  callback fired: fd=" << data->m_sock_fd_ << std::endl;
}

int main() {
    // ========== 1. 空链表 tick 不崩溃 ==========
    std::cout << "=== Test 1: empty list tick ===" << std::endl;
    {
        sort_timer_lst lst;
        lst.tick();  // 应该安全，什么都不发生
    }
    std::cout << "  PASS" << std::endl;

    // ========== 2. add_timer + tick (按顺序插入) ==========
    std::cout << "\n=== Test 2: add ordered timers + tick ===" << std::endl;
    cb_count = 0;
    cb_order.clear();
    {
        sort_timer_lst lst;

        // 创建 client_data（栈上分配即可）
        client_data c1{}, c2{}, c3{};
        c1.m_sock_fd_ = 1; c2.m_sock_fd_ = 2; c3.m_sock_fd_ = 3;

        auto t1 = std::make_shared<util_timer>();
        t1->expire_ = time(nullptr) - 10; // 已过期 10 秒
        t1->cb_func = test_cb;
        t1->user_data_ = &c1;

        auto t2 = std::make_shared<util_timer>();
        t2->expire_ = time(nullptr) - 5;  // 已过期 5 秒
        t2->cb_func = test_cb;
        t2->user_data_ = &c2;

        auto t3 = std::make_shared<util_timer>();
        t3->expire_ = time(nullptr) + 100; // 未到期
        t3->cb_func = test_cb;
        t3->user_data_ = &c3;

        lst.add_timer(t1);
        lst.add_timer(t2);
        lst.add_timer(t3);

        lst.tick();

        // t1 和 t2 应该被触发（按 expire_ 顺序：t1 先）
        assert(cb_count == 2);
        assert(cb_order.size() == 2);
        assert(cb_order[0] == 1); // t1 先触发
        assert(cb_order[1] == 2); // t2 后触发
        // t3 未到期，不应触发
    } // lst 析构，t3 被正确释放
    std::cout << "  PASS" << std::endl;

    // ========== 3. add_timer 乱序插入 (测试排序) ==========
    std::cout << "\n=== Test 3: add unordered timers ===" << std::endl;
    cb_count = 0;
    cb_order.clear();
    {
        sort_timer_lst lst;

        client_data c1{}, c2{}, c3{};
        c1.m_sock_fd_ = 1; c2.m_sock_fd_ = 2; c3.m_sock_fd_ = 3;

        auto t3 = std::make_shared<util_timer>();
        t3->expire_ = time(nullptr) - 10;
        t3->cb_func = test_cb;
        t3->user_data_ = &c3;

        auto t1 = std::make_shared<util_timer>();
        t1->expire_ = time(nullptr) - 30; // 最老
        t1->cb_func = test_cb;
        t1->user_data_ = &c1;

        auto t2 = std::make_shared<util_timer>();
        t2->expire_ = time(nullptr) - 20;
        t2->cb_func = test_cb;
        t2->user_data_ = &c2;

        // 乱序插入：3, 1, 2
        lst.add_timer(t3);
        lst.add_timer(t1);
        lst.add_timer(t2);

        lst.tick();

        assert(cb_count == 3);
        assert(cb_order[0] == 1); // 最老的先触发
        assert(cb_order[1] == 2);
        assert(cb_order[2] == 3);
    }
    std::cout << "  PASS" << std::endl;

    // ========== 4. del_timer 测试 ==========
    std::cout << "\n=== Test 4: del_timer ===" << std::endl;
    cb_count = 0;
    {
        sort_timer_lst lst;

        client_data c1{}, c2{};
        c1.m_sock_fd_ = 1; c2.m_sock_fd_ = 2;

        auto t1 = std::make_shared<util_timer>();
        t1->expire_ = time(nullptr) - 10;
        t1->cb_func = test_cb;
        t1->user_data_ = &c1;

        auto t2 = std::make_shared<util_timer>();
        t2->expire_ = time(nullptr) - 10;
        t2->cb_func = test_cb;
        t2->user_data_ = &c2;

        lst.add_timer(t1);
        lst.add_timer(t2);

        lst.del_timer(t1);  // 删除 t1

        lst.tick();

        assert(cb_count == 1); // 只有 t2 被触发
    }
    std::cout << "  PASS" << std::endl;

    // ========== 5. adjust_timer 测试 ==========
    std::cout << "\n=== Test 5: adjust_timer ===" << std::endl;
    cb_count = 0;
    {
        sort_timer_lst lst;

        client_data c1{};
        c1.m_sock_fd_ = 1;

        auto t1 = std::make_shared<util_timer>();
        t1->expire_ = time(nullptr) - 10; // 已过期
        t1->cb_func = test_cb;
        t1->user_data_ = &c1;

        lst.add_timer(t1);

        // 调整到未来
        t1->expire_ = time(nullptr) + 100;
        lst.adjust_timer(t1);

        lst.tick();

        assert(cb_count == 0); // 不应该被触发，因为被调整到未来了
    }
    std::cout << "  PASS" << std::endl;

    // ========== 6. nullptr 安全测试 ==========
    std::cout << "\n=== Test 6: nullptr safety ===" << std::endl;
    {
        sort_timer_lst lst;
        lst.add_timer(nullptr);     // 不应崩溃
        lst.del_timer(nullptr);     // 不应崩溃
        lst.adjust_timer(nullptr);  // 不应崩溃
    }
    std::cout << "  PASS" << std::endl;

    // ========== 7. 析构测试：长链表无栈溢出 ==========
    std::cout << "\n=== Test 7: destruction of long list ===" << std::endl;
    {
        sort_timer_lst lst;

        for (int i = 0; i < 10000; i++) {
            auto t = std::make_shared<util_timer>();
            t->expire_ = time(nullptr) + i;
            lst.add_timer(t);
        }
        std::cout << "  10000 timers added, destroying..." << std::endl;
    } // 析构应该不栈溢出
    std::cout << "  PASS" << std::endl;

    std::cout << "\n========== All tests passed! ==========" << std::endl;
    return 0;
}