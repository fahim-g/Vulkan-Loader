/*
 * Copyright (c) 2021 The Khronos Group Inc.
 * Copyright (c) 2021 Valve Corporation
 * Copyright (c) 2021 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and/or associated documentation files (the "Materials"), to
 * deal in the Materials without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Materials, and to permit persons to whom the Materials are
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice(s) and this permission notice shall be included in
 * all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE
 * USE OR OTHER DEALINGS IN THE MATERIALS.
 *
 * Author: Charles Giessen <charles@lunarg.com>
 */

#include "test_environment.h"
#include <functional>

/* Test cases which need coverage

* Layer looks for unknown phys dev function
    - make sure app can query the function later on too

* Layer intercepts both device and physical device unknown functions

* Layer which wraps and looks for unknown phys dev functions
    - make sure app can query the function later on too

* Layer can use provided GPDPA or query for it with GIPA and gets the same results

* Make sure existing tests which 'intercept' unknown physical device functions are 'correct'
    - Meaning the call would work with wrapping and calls the right layer(s)

*/

enum TestConfig {
    add_layer_implementation,
    add_layer_interception,
};

bool has_flag(std::vector<TestConfig> const& flags, TestConfig config) {
    for (auto const& flag : flags)
        if (flag == config) return true;
    return false;
}
/*
 Creates a TestICD with a function unknown to the loader called vkNotRealFuncTEST. The TestICD, when vk_icdGetPhysicalDeviceProcAddr
 is called, will return the custom_physical_device_function if the function name matches vkNotRealFuncTEST. The test then calls the
 function to verify that the unknown physical device function dispatching is working correctly.
*/
template <typename DispatchableHandleType>
struct custom_functions {
    static VKAPI_ATTR uint32_t VKAPI_CALL func_zero(DispatchableHandleType handle, uint32_t foo) { return foo; };
    static VKAPI_ATTR uint32_t VKAPI_CALL func_one(DispatchableHandleType handle, uint32_t foo, uint32_t bar) { return foo + bar; };
    static VKAPI_ATTR float VKAPI_CALL func_two(DispatchableHandleType handle, uint32_t foo, uint32_t bar, float baz) {
        return baz + foo + bar;
    };
    static VKAPI_ATTR int VKAPI_CALL func_three(DispatchableHandleType handle, int* ptr_a, int* ptr_b) { return *ptr_a + *ptr_b; };
    static VKAPI_ATTR float VKAPI_CALL func_four(DispatchableHandleType handle, int* ptr_a, int* ptr_b, int foo, int bar, float k,
                                                 float l, char a, char b, char c) {
        return *ptr_a + *ptr_b + foo + bar + k + l + static_cast<int>(a) + static_cast<int>(b) + static_cast<int>(c);
    };
};

/*
Functions for testing of layer interception of unknown functions. Note the need to pass a pointer to the layer and the name of the
called function as a parameter, this is necessary to allow a generic layer implementation, as the layer must look up the function
pointer to use. A real layer would store the function pointer in a dedicated structure per-instance/device, but since the TestLayer
is a generic layer, there isn't a fixed list of functions that should be supported.
*/
template <typename DispatchableHandleType>
struct layer_intercept_functions {
    static PFN_vkVoidFunction find_custom_func(TestLayer* layer, const char* name) {
        if (layer->custom_physical_device_dispatch_functions.count(name) > 0) {
            return layer->custom_physical_device_dispatch_functions.at(name);
        } else if (layer->custom_device_dispatch_functions.count(name) > 0) {
            return layer->custom_device_dispatch_functions.at(name);
        }
        return nullptr;
    }

    static VKAPI_ATTR uint32_t VKAPI_CALL func_zero(DispatchableHandleType handle, TestLayer* layer, const char* name, uint32_t i) {
        auto func = reinterpret_cast<decltype(&func_zero)>(find_custom_func(layer, name));
        if (func == nullptr) return 1337;
        return func(handle, layer, name, i + 3);
    }
    static VKAPI_ATTR uint32_t VKAPI_CALL func_one(DispatchableHandleType handle, TestLayer* layer, const char* name, uint32_t i,
                                                   float f) {
        auto func = reinterpret_cast<decltype(&func_one)>(find_custom_func(layer, name));
        if (func == nullptr) return 1337;
        return func(handle, layer, name, i + 2, f + 1.f);
    }
    static VKAPI_ATTR float VKAPI_CALL func_two(DispatchableHandleType handle, TestLayer* layer, const char* name, uint32_t foo,
                                                uint32_t bar, float baz) {
        auto func = reinterpret_cast<decltype(&func_two)>(find_custom_func(layer, name));
        if (func == nullptr) return -1337;
        return func(handle, layer, name, foo + 1, bar + 2, baz * 2);
    };
    static VKAPI_ATTR int VKAPI_CALL func_three(DispatchableHandleType handle, TestLayer* layer, const char* name, int* ptr_a,
                                                int* ptr_b) {
        auto func = reinterpret_cast<decltype(&func_three)>(find_custom_func(layer, name));
        if (func == nullptr) return -1337;
        *ptr_a += 1;
        *ptr_b -= 2;
        return func(handle, layer, name, ptr_a, ptr_b);
    };
    static VKAPI_ATTR float VKAPI_CALL func_four(DispatchableHandleType handle, TestLayer* layer, const char* name, int* ptr_a,
                                                 int* ptr_b, int foo, int bar, float k, float l, char a, char b, char c) {
        auto func = reinterpret_cast<decltype(&func_four)>(find_custom_func(layer, name));
        if (func == nullptr) return -1337.f;
        return func(handle, layer, name, ptr_a, ptr_b, foo + 4, bar + 5, k + 1, l + 2, 'd', 'e', 'f');
    };
};

template <typename DispatchableHandleType>
struct layer_implementation_functions {
    static VKAPI_ATTR uint32_t VKAPI_CALL func_zero(DispatchableHandleType device, TestLayer* layer, const char* name, uint32_t i) {
        return i * 3;
    }
    static VKAPI_ATTR uint32_t VKAPI_CALL func_one(DispatchableHandleType device, TestLayer* layer, const char* name, uint32_t i,
                                                   float f) {
        return static_cast<int>(i * 3 + f * 10.f);
    }
    static VKAPI_ATTR float VKAPI_CALL func_two(DispatchableHandleType handle, TestLayer* layer, const char* name, uint32_t foo,
                                                uint32_t bar, float baz) {
        return baz + foo + bar;
    };
    static VKAPI_ATTR int VKAPI_CALL func_three(DispatchableHandleType handle, TestLayer* layer, const char* name, int* ptr_a,
                                                int* ptr_b) {
        return *ptr_a + *ptr_b;
    };
    static VKAPI_ATTR float VKAPI_CALL func_four(DispatchableHandleType handle, TestLayer* layer, const char* name, int* ptr_a,
                                                 int* ptr_b, int foo, int bar, float k, float l, char a, char b, char c) {
        return *ptr_a + *ptr_b + foo + bar + k + l + static_cast<int>(a) + static_cast<int>(b) + static_cast<int>(c);
    };
};

// Add function_count strings to the func_names vector, starting at function_start place. Essentially a utility for filling
// up a list of names to use later
void add_function_names(std::vector<std::string>& func_names, uint32_t function_count, uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        func_names.push_back(std::string("vkNotIntRealFuncTEST_") + std::to_string(i++));
        func_names.push_back(std::string("vkNotIntRealIntFuncTEST_") + std::to_string(i++));
        func_names.push_back(std::string("vkIntNotIntRealFloatFuncTEST_") + std::to_string(i++));
        func_names.push_back(std::string("vkNotRealFuncPointerPointerTEST_") + std::to_string(i++));
        func_names.push_back(std::string("vkNotRealFuncTEST_pointer_pointer_int_int_float_float_char_char_char_") +
                             std::to_string(i++));
    }
}

// Add data to the function_list, which could be a driver or a layer list of implementation functions.
template <typename FunctionStruct>
void fill_implementation_functions(std::vector<VulkanFunction>& function_list, std::vector<std::string>& func_names,
                                   FunctionStruct const& funcs, uint32_t function_count, uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        function_list.push_back(VulkanFunction{func_names.at(i++), to_vkVoidFunction(funcs.func_zero)});
        function_list.push_back(VulkanFunction{func_names.at(i++), to_vkVoidFunction(funcs.func_one)});
        function_list.push_back(VulkanFunction{func_names.at(i++), to_vkVoidFunction(funcs.func_two)});
        function_list.push_back(VulkanFunction{func_names.at(i++), to_vkVoidFunction(funcs.func_three)});
        function_list.push_back(VulkanFunction{func_names.at(i++), to_vkVoidFunction(funcs.func_four)});
    }
}

// Add device interception functions to a layer. Need to call `add_custom_device_interception_function` since the layer has to setup
// a unordered_map for storing the next function in the chain, and key it based on the name
template <typename FunctionStruct>
void fill_device_intercept_functions(TestLayer& layer, std::vector<std::string>& func_names, FunctionStruct const& funcs,
                                     uint32_t function_count, uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        layer.add_custom_device_interception_function(func_names.at(i++), to_vkVoidFunction(funcs.func_zero));
        layer.add_custom_device_interception_function(func_names.at(i++), to_vkVoidFunction(funcs.func_one));
        layer.add_custom_device_interception_function(func_names.at(i++), to_vkVoidFunction(funcs.func_two));
        layer.add_custom_device_interception_function(func_names.at(i++), to_vkVoidFunction(funcs.func_three));
        layer.add_custom_device_interception_function(func_names.at(i++), to_vkVoidFunction(funcs.func_four));
    }
}
// Add physical device interception functions to a layer. Need to call `add_custom_device_interception_function` since the layer has
// to setup a unordered_map for storing the next function in the chain, and key it based on the name
template <typename FunctionStruct>
void fill_phys_dev_intercept_functions(TestLayer& layer, std::vector<std::string>& func_names, FunctionStruct const& funcs,
                                       uint32_t function_count, uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        layer.add_custom_physical_device_intercept_function(func_names.at(i++), to_vkVoidFunction(funcs.func_zero));
        layer.add_custom_physical_device_intercept_function(func_names.at(i++), to_vkVoidFunction(funcs.func_one));
        layer.add_custom_physical_device_intercept_function(func_names.at(i++), to_vkVoidFunction(funcs.func_two));
        layer.add_custom_physical_device_intercept_function(func_names.at(i++), to_vkVoidFunction(funcs.func_three));
        layer.add_custom_physical_device_intercept_function(func_names.at(i++), to_vkVoidFunction(funcs.func_four));
    }
}

template <typename FunctionLoader, typename ParentType, typename DispatchableHandleType, typename FunctionStruct>
void check_custom_functions(FunctionLoader& loader, ParentType parent, DispatchableHandleType handle, FunctionStruct const& s,
                            std::vector<std::string>& func_names, uint32_t function_count, uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        decltype(FunctionStruct::func_zero)* returned_func_i = loader.load(parent, func_names.at(i++).c_str());
        ASSERT_NE(returned_func_i, nullptr);
        EXPECT_EQ(returned_func_i(handle, i * 10), i * 10);

        decltype(FunctionStruct::func_one)* returned_func_ii = loader.load(parent, func_names.at(i++).c_str());
        ASSERT_NE(returned_func_ii, nullptr);
        EXPECT_EQ(returned_func_ii(handle, i * 10, i * 5), i * 10 + i * 5);

        decltype(FunctionStruct::func_two)* returned_func_iif = loader.load(parent, func_names.at(i++).c_str());
        ASSERT_NE(returned_func_iif, nullptr);
        EXPECT_NEAR(returned_func_iif(handle, i * 10, i * 5, 0.1234f), i * 10 + i * 5 + 0.1234f, 0.001);

        int x = 5;
        int y = -505;
        decltype(FunctionStruct::func_three)* returned_func_pp = loader.load(parent, func_names.at(i++).c_str());
        ASSERT_NE(returned_func_pp, nullptr);
        EXPECT_EQ(returned_func_pp(handle, &x, &y), -500);

        x = 5;
        y = -505;
        decltype(FunctionStruct::func_four)* returned_func_ppiiffccc = loader.load(parent, func_names.at(i++).c_str());
        ASSERT_NE(returned_func_ppiiffccc, nullptr);
        EXPECT_NEAR(returned_func_ppiiffccc(handle, &x, &y, 200, 300, 0.123f, 1001.89f, 'a', 'b', 'c'),
                    -500 + 200 + 300 + 0.123 + 1001.89 + 97 + 98 + 99, 0.001f);
    }
}

template <typename FunctionLoader, typename ParentType, typename DispatchableHandleType, typename FunctionStruct>
void check_layer_custom_functions(FunctionLoader& loader, ParentType parent, DispatchableHandleType handle, TestLayer& layer,
                                  FunctionStruct const& s, std::vector<std::string>& func_names, uint32_t function_count,
                                  uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        decltype(FunctionStruct::func_zero)* returned_func_i = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_i, nullptr);
        EXPECT_EQ(returned_func_i(handle, &layer, func_names.at(i).c_str(), i), (i + 3) * 3);
        i++;
        decltype(FunctionStruct::func_one)* returned_func_if = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_if, nullptr);
        EXPECT_EQ(returned_func_if(handle, &layer, func_names.at(i).c_str(), i, i + 1.f), (i + 2) * 3 + (i + 2) * 10);
        i++;

        decltype(FunctionStruct::func_two)* returned_func_iif = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_iif, nullptr);
        EXPECT_NEAR(returned_func_iif(handle, &layer, func_names.at(i).c_str(), i * 10, i * 5, 0.1234f),
                    (i * 10 + 1) + (i * 5 + 2) + (0.1234f * 2.f), 0.001);
        i++;

        int x = 5 + i;
        int y = -505 - i;
        decltype(FunctionStruct::func_three)* returned_func_pp = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_pp, nullptr);
        EXPECT_EQ(returned_func_pp(handle, &layer, func_names.at(i).c_str(), &x, &y),
                  (5 + static_cast<int>(i) + 1) + (-505 - static_cast<int>(i) - 2));
        i++;

        x = 5;
        y = -505;
        decltype(FunctionStruct::func_four)* returned_func_ppiiffccc = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_ppiiffccc, nullptr);
        EXPECT_NEAR(
            returned_func_ppiiffccc(handle, &layer, func_names.at(i).c_str(), &x, &y, 200, 300, 0.123f, 1001.89f, 'a', 'b', 'c'),
            -500 + (200 + 4) + (300 + 5) + (0.123 + 1) + (1001.89 + 2) + 100 + 101 + 102, 0.001f);  // layer changes abc to def
        i++;
    }
}

template <typename FunctionLoader, typename ParentType, typename DispatchableHandleType, typename FunctionStruct>
void check_layer_custom_functions_no_implementation(FunctionLoader& loader, ParentType parent, DispatchableHandleType handle,
                                                    TestLayer& layer, FunctionStruct const& s, std::vector<std::string>& func_names,
                                                    uint32_t function_count, uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        decltype(FunctionStruct::func_zero)* returned_func_i = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_i, nullptr);
        EXPECT_EQ(1337U, returned_func_i(handle, &layer, func_names.at(i).c_str(), i));
        i++;
        decltype(FunctionStruct::func_one)* returned_func_if = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_if, nullptr);
        EXPECT_EQ(1337U, returned_func_if(handle, &layer, func_names.at(i).c_str(), i, i + 1.f));
        i++;

        decltype(FunctionStruct::func_two)* returned_func_iif = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_iif, nullptr);
        EXPECT_NEAR(-1337.0, returned_func_iif(handle, &layer, func_names.at(i).c_str(), i * 10, i * 5, 0.1234f), 0.001);
        i++;

        int x = 5 + i;
        int y = -505 - i;
        decltype(FunctionStruct::func_three)* returned_func_pp = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_pp, nullptr);
        EXPECT_EQ(-1337, returned_func_pp(handle, &layer, func_names.at(i).c_str(), &x, &y));
        i++;

        x = 5;
        y = -505;
        decltype(FunctionStruct::func_four)* returned_func_ppiiffccc = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_ppiiffccc, nullptr);
        EXPECT_NEAR(
            -1337.0,
            returned_func_ppiiffccc(handle, &layer, func_names.at(i).c_str(), &x, &y, 200, 300, 0.123f, 1001.89f, 'a', 'b', 'c'),
            0.001);
        i++;
    }
}

template <typename FunctionLoader, typename ParentType, typename DispatchableHandleType, typename FunctionStruct>
void check_layer_custom_functions_no_interception(FunctionLoader& loader, ParentType parent, DispatchableHandleType handle,
                                                  TestLayer& layer, FunctionStruct const& s, std::vector<std::string>& func_names,
                                                  uint32_t function_count, uint32_t function_start = 0) {
    for (uint32_t i = function_start; i < function_start + function_count;) {
        decltype(FunctionStruct::func_zero)* returned_func_i = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_i, nullptr);
        EXPECT_EQ(returned_func_i(handle, &layer, func_names.at(i).c_str(), i), (i)*3);
        i++;
        decltype(FunctionStruct::func_one)* returned_func_if = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_if, nullptr);
        EXPECT_EQ(returned_func_if(handle, &layer, func_names.at(i).c_str(), i, i + 1.f), (i)*3 + (i + 1) * 10);
        i++;

        decltype(FunctionStruct::func_two)* returned_func_iif = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_iif, nullptr);
        EXPECT_NEAR(returned_func_iif(handle, &layer, func_names.at(i).c_str(), i * 10, i * 5, 0.1234f),
                    (i * 10) + (i * 5) + (0.1234f), 0.001);
        i++;

        int x = 5 + i;
        int y = -505 - i;
        decltype(FunctionStruct::func_three)* returned_func_pp = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_pp, nullptr);
        EXPECT_EQ(returned_func_pp(handle, &layer, func_names.at(i).c_str(), &x, &y),
                  (5 + static_cast<int>(i)) + (-505 - static_cast<int>(i)));
        i++;

        x = 5;
        y = -505;
        decltype(FunctionStruct::func_four)* returned_func_ppiiffccc = loader.load(parent, func_names.at(i).c_str());
        ASSERT_NE(returned_func_ppiiffccc, nullptr);
        EXPECT_NEAR(
            returned_func_ppiiffccc(handle, &layer, func_names.at(i).c_str(), &x, &y, 200, 300, 0.123f, 1001.89f, 'a', 'b', 'c'),
            -500 + (200) + (300) + (0.123) + (1001.89) + 97 + 98 + 99, 0.001f);
        i++;
    }
}

using custom_physical_device_functions = custom_functions<VkPhysicalDevice>;
using layer_intercept_physical_device_functions = layer_intercept_functions<VkPhysicalDevice>;
using layer_implementation_physical_device_functions = layer_implementation_functions<VkPhysicalDevice>;

TEST(UnknownFunction, PhysicalDeviceFunction) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    auto& driver = env.get_test_icd();
    uint32_t function_count = MAX_NUM_UNKNOWN_EXTS;
    std::vector<std::string> function_names;
    add_function_names(function_names, function_count);

    driver.physical_devices.emplace_back("physical_device_0");
    fill_implementation_functions(driver.custom_physical_device_functions, function_names, custom_physical_device_functions{},
                                  function_count);
    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    VkPhysicalDevice phys_dev = inst.GetPhysDev();
    check_custom_functions(env.vulkan_functions, inst.inst, phys_dev, custom_physical_device_functions{}, function_names,
                           function_count);
}

TEST(UnknownFunction, PhysicalDeviceFunctionMultipleDriverSupport) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    auto& driver_0 = env.get_test_icd(0);
    auto& driver_1 = env.get_test_icd(1);
    uint32_t function_count = MAX_NUM_UNKNOWN_EXTS;
    std::vector<std::string> function_names;
    add_function_names(function_names, function_count);

    // used to identify the GPUs
    VkPhysicalDeviceProperties props{};
    driver_0.physical_devices.emplace_back("physical_device_0");
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    driver_0.physical_devices.back().set_properties(props);
    driver_1.physical_devices.emplace_back("physical_device_1");
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    driver_1.physical_devices.back().set_properties(props);

    for (uint32_t i = 0; i < function_count / 10; i++) {
        fill_implementation_functions(driver_0.custom_physical_device_functions, function_names, custom_physical_device_functions{},
                                      5, i * 10);
        fill_implementation_functions(driver_1.custom_physical_device_functions, function_names, custom_physical_device_functions{},
                                      5, i * 10 + 5);
    }
    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    auto phys_devs = inst.GetPhysDevs(2);
    VkPhysicalDevice phys_dev_0 = phys_devs[0];
    VkPhysicalDevice phys_dev_1 = phys_devs[1];
    env.vulkan_functions.vkGetPhysicalDeviceProperties(phys_devs[0], &props);
    if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        phys_dev_0 = phys_devs[1];
        phys_dev_1 = phys_devs[0];
    }
    for (uint32_t i = 0; i < function_count / 10; i++) {
        check_custom_functions(env.vulkan_functions, inst.inst, phys_dev_0, custom_physical_device_functions{}, function_names, 5,
                               i * 10);
        check_custom_functions(env.vulkan_functions, inst.inst, phys_dev_1, custom_physical_device_functions{}, function_names, 5,
                               i * 10 + 5);
    }
}

// Add unknown functions to driver 0, and try to use them on driver 1.
TEST(UnknownFunctionDeathTests, PhysicalDeviceFunctionErrorPath) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    auto& driver_0 = env.get_test_icd(0);
    auto& driver_1 = env.get_test_icd(1);
    std::vector<std::string> function_names;
    add_function_names(function_names, 1);

    // used to identify the GPUs
    VkPhysicalDeviceProperties props{};
    driver_0.physical_devices.emplace_back("physical_device_0");
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    driver_0.physical_devices.back().set_properties(props);
    driver_1.physical_devices.emplace_back("physical_device_1");
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    driver_1.physical_devices.back().set_properties(props);
    function_names.push_back(std::string("vkNotIntRealFuncTEST_0"));

    custom_physical_device_functions funcs{};
    driver_0.custom_physical_device_functions.push_back(VulkanFunction{function_names.back(), to_vkVoidFunction(funcs.func_zero)});

    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    auto phys_devs = inst.GetPhysDevs(2);
    VkPhysicalDevice phys_dev_to_use = phys_devs[1];
    env.vulkan_functions.vkGetPhysicalDeviceProperties(phys_devs[1], &props);
    if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) phys_dev_to_use = phys_devs[0];
    // use the wrong GPU to query the functions, should get 5 errors

    decltype(custom_physical_device_functions::func_zero)* returned_func_i =
        env.vulkan_functions.load(inst.inst, function_names.at(0).c_str());
    ASSERT_NE(returned_func_i, nullptr);
    ASSERT_DEATH(returned_func_i(phys_dev_to_use, 0), "Extension vkNotIntRealFuncTEST_0 not supported for this physical device");
}

TEST(UnknownFunction, PhysicalDeviceFunctionWithImplicitLayerImplementation) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    uint32_t function_count = MAX_NUM_UNKNOWN_EXTS;
    auto& driver = env.get_test_icd();
    std::vector<std::string> function_names;
    add_function_names(function_names, function_count);

    driver.physical_devices.emplace_back("physical_device_0");

    env.add_implicit_layer(ManifestLayer{}.add_layer(ManifestLayer::LayerDescription{}
                                                         .set_name("VK_LAYER_implicit_layer_unknown_function_intercept")
                                                         .set_lib_path(TEST_LAYER_PATH_EXPORT_VERSION_2)
                                                         .set_disable_environment("DISABLE_ME")),
                           "implicit_layer_unknown_function_intercept.json");
    auto& layer = env.get_test_layer();
    fill_implementation_functions(layer.custom_physical_device_implementation_functions, function_names,
                                  layer_implementation_physical_device_functions{}, function_count);

    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    VkPhysicalDevice phys_dev = inst.GetPhysDev();
    check_layer_custom_functions_no_interception(env.vulkan_functions, inst.inst, phys_dev, layer,
                                                 layer_implementation_physical_device_functions{}, function_names, function_count);
}

TEST(UnknownFunction, PhysicalDeviceFunctionMultipleDriverSupportWithImplicitLayerImplementation) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    auto& driver_0 = env.get_test_icd(0);
    auto& driver_1 = env.get_test_icd(1);
    uint32_t function_count = MAX_NUM_UNKNOWN_EXTS;
    std::vector<std::string> function_names;
    add_function_names(function_names, function_count);

    // used to identify the GPUs
    VkPhysicalDeviceProperties props{};
    driver_0.physical_devices.emplace_back("physical_device_0");
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    driver_0.physical_devices.back().set_properties(props);
    driver_1.physical_devices.emplace_back("physical_device_1");
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    driver_1.physical_devices.back().set_properties(props);
    for (uint32_t i = 0; i < function_count / 10; i++) {
        fill_implementation_functions(driver_0.custom_physical_device_functions, function_names, custom_physical_device_functions{},
                                      5, i * 10);
        fill_implementation_functions(driver_1.custom_physical_device_functions, function_names, custom_physical_device_functions{},
                                      5, i * 10 + 5);
    }

    env.add_implicit_layer(ManifestLayer{}.add_layer(ManifestLayer::LayerDescription{}
                                                         .set_name("VK_LAYER_implicit_layer_unknown_function_intercept")
                                                         .set_lib_path(TEST_LAYER_PATH_EXPORT_VERSION_2)
                                                         .set_disable_environment("DISABLE_ME")),
                           "implicit_layer_unknown_function_intercept.json");

    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    auto phys_devs = inst.GetPhysDevs(2);
    VkPhysicalDevice phys_dev_0 = phys_devs[0];
    VkPhysicalDevice phys_dev_1 = phys_devs[1];
    env.vulkan_functions.vkGetPhysicalDeviceProperties(phys_devs[0], &props);
    if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        phys_dev_0 = phys_devs[1];
        phys_dev_1 = phys_devs[0];
    }
    for (uint32_t i = 0; i < function_count / 10; i++) {
        check_custom_functions(env.vulkan_functions, inst.inst, phys_dev_0, custom_physical_device_functions{}, function_names, 5,
                               i * 10);
        check_custom_functions(env.vulkan_functions, inst.inst, phys_dev_1, custom_physical_device_functions{}, function_names, 5,
                               i * 10 + 5);
    }
}

TEST(UnknownFunction, PhysicalDeviceFunctionWithImplicitLayerInterception) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    uint32_t function_count = MAX_NUM_UNKNOWN_EXTS;
    auto& driver = env.get_test_icd();
    driver.physical_devices.emplace_back("physical_device_0");

    std::vector<std::string> function_names;
    add_function_names(function_names, function_count);

    env.add_implicit_layer(ManifestLayer{}.add_layer(ManifestLayer::LayerDescription{}
                                                         .set_name("VK_LAYER_implicit_layer_unknown_function_intercept")
                                                         .set_lib_path(TEST_LAYER_PATH_EXPORT_VERSION_2)
                                                         .set_disable_environment("DISABLE_ME")),
                           "implicit_layer_unknown_function_intercept.json");
    auto& layer = env.get_test_layer();
    fill_phys_dev_intercept_functions(layer, function_names, layer_intercept_physical_device_functions{}, function_count);

    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    VkPhysicalDevice phys_dev = inst.GetPhysDev();
    check_layer_custom_functions_no_implementation(env.vulkan_functions, inst.inst, phys_dev, layer,
                                                   layer_intercept_physical_device_functions{}, function_names, function_count);
}

TEST(UnknownFunction, PhysicalDeviceFunctionDriverSupportWithImplicitLayerInterception) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    auto& driver = env.get_test_icd();
    uint32_t function_count = 100;
    std::vector<std::string> function_names;
    add_function_names(function_names, function_count);
    driver.physical_devices.emplace_back("physical_device_0");
    fill_implementation_functions(driver.custom_physical_device_functions, function_names,
                                  layer_implementation_physical_device_functions{}, function_count);
    env.add_implicit_layer(ManifestLayer{}.add_layer(ManifestLayer::LayerDescription{}
                                                         .set_name("VK_LAYER_implicit_layer_unknown_function_intercept")
                                                         .set_lib_path(TEST_LAYER_PATH_EXPORT_VERSION_2)
                                                         .set_disable_environment("DISABLE_ME")),
                           "implicit_layer_unknown_function_intercept.json");
    auto& layer = env.get_test_layer();
    fill_phys_dev_intercept_functions(layer, function_names, layer_intercept_physical_device_functions{}, function_count);

    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    VkPhysicalDevice phys_dev = inst.GetPhysDev();
    check_layer_custom_functions(env.vulkan_functions, inst.inst, phys_dev, layer, layer_intercept_physical_device_functions{},
                                 function_names, function_count);
}

TEST(UnknownFunction, PhysicalDeviceFunctionWithMultipleImplicitLayersInterception) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    auto& driver = env.get_test_icd();
    std::vector<std::string> function_names;
    uint32_t function_count = MAX_NUM_UNKNOWN_EXTS;
    add_function_names(function_names, function_count);
    driver.physical_devices.emplace_back("physical_device_0");

    env.add_implicit_layer(ManifestLayer{}.add_layer(ManifestLayer::LayerDescription{}
                                                         .set_name("VK_LAYER_implicit_layer_unknown_function_intercept_0")
                                                         .set_lib_path(TEST_LAYER_PATH_EXPORT_VERSION_2)
                                                         .set_disable_environment("DISABLE_ME")),
                           "implicit_layer_unknown_function_intercept_0.json");
    auto& layer_0 = env.get_test_layer(0);
    layer_0.set_use_gipa_GetPhysicalDeviceProcAddr(true);
    env.add_implicit_layer(ManifestLayer{}.add_layer(ManifestLayer::LayerDescription{}
                                                         .set_name("VK_LAYER_implicit_layer_unknown_function_intercept_1")
                                                         .set_lib_path(TEST_LAYER_PATH_EXPORT_VERSION_2)
                                                         .set_disable_environment("DISABLE_ME")),
                           "implicit_layer_unknown_function_intercept_1.json");
    auto& layer_1 = env.get_test_layer(1);
    layer_1.set_use_gipa_GetPhysicalDeviceProcAddr(false);
    for (uint32_t i = 0; i < function_count / 10; i++) {
        fill_implementation_functions(driver.custom_physical_device_functions, function_names,
                                      layer_implementation_physical_device_functions{}, 5, i * 10);
        fill_phys_dev_intercept_functions(layer_0, function_names, layer_intercept_physical_device_functions{}, 5, i * 10);
        fill_phys_dev_intercept_functions(layer_1, function_names, layer_intercept_physical_device_functions{}, 5, i * 10 + 5);
    }
    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    VkPhysicalDevice phys_dev = inst.GetPhysDev();
    for (uint32_t i = 0; i < function_count / 10; i++) {
        check_layer_custom_functions(env.vulkan_functions, inst.inst, phys_dev, layer_0,
                                     layer_intercept_physical_device_functions{}, function_names, 5, i * 10);
        check_layer_custom_functions_no_implementation(env.vulkan_functions, inst.inst, phys_dev, layer_1,
                                                       layer_intercept_physical_device_functions{}, function_names, 5, i * 10 + 5);
    }
}

template <typename ParentType>
ParentType get_parent_type(InstWrapper const& inst, DeviceWrapper const& dev);

template <>
VkInstance get_parent_type<VkInstance>(InstWrapper const& inst, DeviceWrapper const& dev) {
    return inst.inst;
}
template <>
VkDevice get_parent_type<VkDevice>(InstWrapper const& inst, DeviceWrapper const& dev) {
    return dev.dev;
}

template <typename DispatchableHandleType>
DispatchableHandleType get_dispatch_handle(FrameworkEnvironment& env, DeviceWrapper const& dev,
                                           std::vector<TestConfig> const& flags);

template <>
VkDevice get_dispatch_handle<VkDevice>(FrameworkEnvironment& env, DeviceWrapper const& dev, std::vector<TestConfig> const& flags) {
    return dev.dev;
}

template <>
VkCommandBuffer get_dispatch_handle<VkCommandBuffer>(FrameworkEnvironment& env, DeviceWrapper const& dev,
                                                     std::vector<TestConfig> const& flags) {
    VkCommandPool command_pool;
    VkCommandPoolCreateInfo pool_create_info{};
    DeviceFunctions funcs{env.vulkan_functions, dev};
    funcs.vkCreateCommandPool(dev, &pool_create_info, nullptr, &command_pool);
    VkCommandBuffer command_buffer;
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.commandBufferCount = 1;
    alloc_info.commandPool = command_pool;
    funcs.vkAllocateCommandBuffers(dev, &alloc_info, &command_buffer);
    return command_buffer;
}

template <>
VkQueue get_dispatch_handle<VkQueue>(FrameworkEnvironment& env, DeviceWrapper const& dev, std::vector<TestConfig> const& flags) {
    DeviceFunctions funcs{env.vulkan_functions, dev.dev};
    VkQueue queue;
    funcs.vkGetDeviceQueue(dev, 0, 0, &queue);
    return queue;
}

template <typename ParentType, typename DispatchableHandleType>
void unknown_function_test_impl(std::vector<TestConfig> const& flags) {
    using custom_functions_type = custom_functions<DispatchableHandleType>;
    using layer_implementation_functions_type = layer_implementation_functions<DispatchableHandleType>;
    using layer_intercept_functions_type = layer_intercept_functions<DispatchableHandleType>;

    FrameworkEnvironment env{};
    env.add_icd(TestICDDetails(TEST_ICD_PATH_VERSION_2_EXPORT_ICD_GPDPA));
    uint32_t function_count = MAX_NUM_UNKNOWN_EXTS;

    auto& driver = env.get_test_icd();
    driver.physical_devices.emplace_back("physical_device_0");
    driver.physical_devices.back().add_queue_family_properties({});
    std::vector<std::string> function_names;
    add_function_names(function_names, function_count);

    if (has_flag(flags, TestConfig::add_layer_interception)) {
        fill_implementation_functions(driver.physical_devices.back().known_device_functions, function_names,
                                      layer_implementation_functions_type{}, function_count);
    } else {
        fill_implementation_functions(driver.physical_devices.back().known_device_functions, function_names,
                                      custom_functions_type{}, function_count);
    }
    TestLayer* layer_ptr = nullptr;
    if (has_flag(flags, TestConfig::add_layer_implementation) || has_flag(flags, TestConfig::add_layer_interception)) {
        env.add_implicit_layer(ManifestLayer{}.add_layer(ManifestLayer::LayerDescription{}
                                                             .set_name("VK_LAYER_implicit_layer_unknown_function_intercept")
                                                             .set_lib_path(TEST_LAYER_PATH_EXPORT_VERSION_2)
                                                             .set_disable_environment("DISABLE_ME")),
                               "implicit_layer_unknown_function_intercept.json");
        layer_ptr = &env.get_test_layer();
    }
    if (has_flag(flags, TestConfig::add_layer_implementation) && has_flag(flags, TestConfig::add_layer_interception)) {
        for (uint32_t i = 0; i < function_count / 10; i++) {
            fill_implementation_functions(layer_ptr->custom_device_implementation_functions, function_names,
                                          layer_implementation_functions_type{}, 5, i * 10);
            fill_device_intercept_functions(*layer_ptr, function_names, layer_intercept_functions_type{}, 5, i * 10 + 5);
        }
    } else if (has_flag(flags, TestConfig::add_layer_implementation)) {
        fill_implementation_functions(driver.physical_devices.back().known_device_functions, function_names,
                                      custom_functions_type{}, function_count);
    } else if (has_flag(flags, TestConfig::add_layer_interception)) {
        fill_device_intercept_functions(*layer_ptr, function_names, layer_intercept_functions_type{}, function_count);
    }

    InstWrapper inst{env.vulkan_functions};
    inst.CheckCreate();

    DeviceWrapper dev{inst};
    dev.create_info.add_device_queue({});
    dev.CheckCreate(inst.GetPhysDev());
    auto dispatch_type = get_dispatch_handle<DispatchableHandleType>(env, dev, flags);
    auto parent_type = get_parent_type<ParentType>(inst, dev);

    if (has_flag(flags, TestConfig::add_layer_implementation) && has_flag(flags, TestConfig::add_layer_interception)) {
        for (uint32_t i = 0; i < function_count / 10; i++) {
            check_layer_custom_functions_no_interception(env.vulkan_functions, parent_type, dispatch_type, *layer_ptr,
                                                         layer_implementation_functions_type{}, function_names, 5, i * 10);
        }
    } else if (has_flag(flags, TestConfig::add_layer_interception)) {
        check_layer_custom_functions(env.vulkan_functions, parent_type, dispatch_type, *layer_ptr, layer_intercept_functions_type{},
                                     function_names, function_count);

    } else {
        check_custom_functions(env.vulkan_functions, parent_type, dispatch_type, custom_functions_type{}, function_names,
                               function_count);
    }
}

// Device

TEST(UnknownFunction, DeviceFromGDPA) { unknown_function_test_impl<VkDevice, VkDevice>({}); }

TEST(UnknownFunction, DeviceFromGDPAWithLayerImplementation) {
    unknown_function_test_impl<VkDevice, VkDevice>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, DeviceFromGDPAWithLayerInterception) {
    unknown_function_test_impl<VkDevice, VkDevice>({TestConfig::add_layer_interception});
}

TEST(UnknownFunction, DeviceFromGDPAWithLayerInterceptionAndLayerImplementation) {
    unknown_function_test_impl<VkDevice, VkDevice>({TestConfig::add_layer_interception, TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, DeviceFromGIPA) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkDevice>({});
}

TEST(UnknownFunction, DeviceFromGIPAWithLayerImplementation) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkDevice>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, DeviceFromGIPAWithLayerInterception) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkDevice>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, DeviceFromGIPAWithLayerInterceptionAndLayerImplementation) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkDevice>({TestConfig::add_layer_interception, TestConfig::add_layer_implementation});
}

// Command buffers

TEST(UnknownFunction, CommandBufferFromGDPA) { unknown_function_test_impl<VkDevice, VkCommandBuffer>({}); }

TEST(UnknownFunction, CommandBufferFromGDPAWithLayerImplementation) {
    unknown_function_test_impl<VkDevice, VkCommandBuffer>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, CommandBufferFromGDPAWithLayerInterception) {
    unknown_function_test_impl<VkDevice, VkCommandBuffer>({TestConfig::add_layer_interception});
}

TEST(UnknownFunction, CommandBufferFromGDPAWithLayerInterceptionAndLayerImplementation) {
    unknown_function_test_impl<VkDevice, VkCommandBuffer>(
        {TestConfig::add_layer_interception, TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, CommandBufferFromGIPA) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkCommandBuffer>({});
}

TEST(UnknownFunction, CommandBufferFromGIPAWithLayerImplementation) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkCommandBuffer>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, CommandBufferFromGIPAWithLayerInterception) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkCommandBuffer>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, CommandBufferFromGIPAWithLayerInterceptionAndLayerImplementation) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkCommandBuffer>(
        {TestConfig::add_layer_interception, TestConfig::add_layer_implementation});
}

// Queues

TEST(UnknownFunction, QueueFromGDPA) { unknown_function_test_impl<VkDevice, VkQueue>({}); }

TEST(UnknownFunction, QueueFromGDPAWithLayerImplementation) {
    unknown_function_test_impl<VkDevice, VkQueue>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, QueueFromGDPAWithLayerInterception) {
    unknown_function_test_impl<VkDevice, VkQueue>({TestConfig::add_layer_interception});
}

TEST(UnknownFunction, QueueFromGDPAWithLayerInterceptionAndLayerImplementation) {
    unknown_function_test_impl<VkDevice, VkQueue>({TestConfig::add_layer_interception, TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, QueueFromGIPA) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkQueue>({});
}

TEST(UnknownFunction, QueueFromGIPAWithLayer) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkQueue>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, QueueFromGIPAWithLayerInterception) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkQueue>({TestConfig::add_layer_implementation});
}

TEST(UnknownFunction, QueueFromGIPAWithLayerInterceptionAndLayerImplementation) {
#if defined(__APPLE__)
    GTEST_SKIP() << "Skip this test as currently macOS doesn't fully support unknown functions.";
#endif
    unknown_function_test_impl<VkInstance, VkQueue>({TestConfig::add_layer_interception, TestConfig::add_layer_implementation});
}
