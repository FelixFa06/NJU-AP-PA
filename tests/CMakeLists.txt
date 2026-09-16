# ---------------------------------------------------------------------------
# 测试的组织方式
#
# 每个测试都带一个 CTest 标签，标签就是「它属于哪个阶段」：
#
#   framework  框架自身的回归（协议、事件循环、传输、CLI）
#   stage1     阶段一：会话与大厅
#   stage2     阶段二：基础规则
#   stage3     阶段三：拓展规则
#
# 你不需要读测试代码：在仓库根目录运行 ./run-tests.sh 1|2|3|all 即可，
# 它会配置、编译并只跑对应标签的测试。
# ---------------------------------------------------------------------------

# --- 框架回归：骨架状态下就该全绿 -----------------------------------------
foreach(case_name message_test loop_test transport_test)
    add_executable(${case_name} ${case_name}.cpp)
    target_link_libraries(${case_name} PRIVATE uno_core)
    add_test(NAME ${case_name} COMMAND ${case_name})
    set_tests_properties(${case_name} PROPERTIES LABELS framework)
endforeach()

add_executable(phase1_lobby_test phase1_lobby_test.cpp)
target_link_libraries(phase1_lobby_test PRIVATE uno_core)
add_test(NAME phase1_lobby_test COMMAND phase1_lobby_test)
set_tests_properties(phase1_lobby_test PROPERTIES LABELS "framework;stage1")

# 进程级 shell 驱动：它只验证命令表与帮助文本，不验证游戏规则。
add_executable(uno_shell_driver shell_driver.cpp)
target_link_libraries(uno_shell_driver PRIVATE uno_core)

find_program(BASH_EXECUTABLE bash)
if(BASH_EXECUTABLE)
    add_test(
        NAME phase1_commands
        COMMAND ${BASH_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/shell_commands_test.sh
                $<TARGET_FILE:uno_shell_driver>
    )
    add_test(
        NAME cli_seed
        COMMAND ${BASH_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/cli_seed_test.sh
                $<TARGET_FILE:uno>
    )
    add_test(
        NAME room_cli
        COMMAND ${BASH_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/room_cli_test.sh
                $<TARGET_FILE:uno>
    )
    set_tests_properties(phase1_commands room_cli PROPERTIES LABELS "framework;stage1")
    set_tests_properties(cli_seed PROPERTIES LABELS framework)
endif()

# --- 评分测试：按阶段分组 ---------------------------------------------------
#
# 它们检查的是「你的实现的结果」，所以骨架状态下必然失败。
# 只有显式打开 UNO_GRADING_TESTS 才会构建（run-tests.sh 会自动打开）。
option(UNO_GRADING_TESTS "Build the stage 1/2/3 grading programs" OFF)
if(UNO_GRADING_TESTS)
    foreach(case_name phase2_basic_test phase3_rules_test rules_scenario_test
                      deck_scenario_test)
        add_executable(${case_name} ${case_name}.cpp)
        target_link_libraries(${case_name} PRIVATE uno_core)
        target_include_directories(${case_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME ${case_name} COMMAND ${case_name})
    endforeach()
    set_tests_properties(phase2_basic_test rules_scenario_test
                         PROPERTIES LABELS stage2)
    set_tests_properties(phase3_rules_test deck_scenario_test
                         PROPERTIES LABELS stage3)

    if(BASH_EXECUTABLE)
        add_test(
            NAME room_scenario
            COMMAND ${BASH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/room_scenario.sh
                    $<TARGET_FILE:uno>
                    $<TARGET_FILE:uno_score>
        )
        set_tests_properties(room_scenario PROPERTIES LABELS stage1)
    endif()
endif()

# --- 评分文件工具：给 shell 写的测试用 -------------------------------------
add_executable(uno_score score_tool.cpp)
target_link_libraries(uno_score PRIVATE uno_core)
target_include_directories(uno_score PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
