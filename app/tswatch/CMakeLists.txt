# ##############################################################################
# packages/demos/contest2026_392_hello_app (tswatch) CMakeLists.txt
#
# TsWatch - 联网语音智能体手表应用
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# ##############################################################################

if(CONFIG_TSWATCH)
  set(TSWATCH_DIR ${CMAKE_CURRENT_SOURCE_DIR})

  set(PROGNAME tswatch)
  set(PRIORITY 100)
  set(STACKSIZE 32768)
  set(MODULE ${CONFIG_TSWATCH})

  # 收集各模块源文件（与 Makefile 的 wildcard 对应，缺失目录自动为空）
  file(GLOB MAIN_PAGE_SRCS     ${TSWATCH_DIR}/main_page/src/*.c
                               ${TSWATCH_DIR}/main_page/res/images/*.c)
  file(GLOB APPLIST_SRCS       ${TSWATCH_DIR}/applist/src/*.c
                               ${TSWATCH_DIR}/applist/res/*.c)
  file(GLOB CLOCK_SRCS         ${TSWATCH_DIR}/clock/src/*.c
                               ${TSWATCH_DIR}/clock/res/*.c)
  file(GLOB TIMER_SRCS         ${TSWATCH_DIR}/timer/src/*.c
                               ${TSWATCH_DIR}/timer/res/*.c)
  file(GLOB SETTINGS_SRCS      ${TSWATCH_DIR}/settings/src/*.c
                               ${TSWATCH_DIR}/settings/res/*.c)
  file(GLOB STOPWATCH_SRCS     ${TSWATCH_DIR}/stopwatch/src/*.c
                               ${TSWATCH_DIR}/stopwatch/res/*.c)
  file(GLOB SLEEP_SRCS         ${TSWATCH_DIR}/sleep/src/*.c
                               ${TSWATCH_DIR}/sleep/res/*.c)
  file(GLOB HEART_SRCS         ${TSWATCH_DIR}/heart/src/*.c
                               ${TSWATCH_DIR}/heart/res/*.c)
  file(GLOB EXERCISE_SRCS      ${TSWATCH_DIR}/exercise/src/*.c
                               ${TSWATCH_DIR}/exercise/res/*.c)
  file(GLOB UTILS_SRCS         ${TSWATCH_DIR}/utils/src/*.c
                               ${TSWATCH_DIR}/utils/res/*.c)
  file(GLOB XIAOZHI_VOICE_SRCS ${TSWATCH_DIR}/xiaozhi_voice/*.c)

  set(CSRCS
      ${MAIN_PAGE_SRCS}
      ${APPLIST_SRCS}
      ${CLOCK_SRCS}
      ${TIMER_SRCS}
      ${SETTINGS_SRCS}
      ${STOPWATCH_SRCS}
      ${SLEEP_SRCS}
      ${HEART_SRCS}
      ${EXERCISE_SRCS}
      ${UTILS_SRCS}
      ${XIAOZHI_VOICE_SRCS}
  )
  set(MAINSRC watch_main.c)

  # include 目录（与 Makefile 的 CFLAGS += -I... 对应）
  set(INCDIR
      ${TSWATCH_DIR}/main_page/include
      ${TSWATCH_DIR}/applist/include
      ${TSWATCH_DIR}/clock/include
      ${TSWATCH_DIR}/timer/include
      ${TSWATCH_DIR}/settings/include
      ${TSWATCH_DIR}/stopwatch/include
      ${TSWATCH_DIR}/sleep/include
      ${TSWATCH_DIR}/heart/include
      ${TSWATCH_DIR}/exercise/include
      ${TSWATCH_DIR}/utils/include
      ${TSWATCH_DIR}/xiaozhi_voice
      ${NUTTX_APPS_DIR}/external/opus/opus/include
  )

  nuttx_add_application(
    NAME ${PROGNAME}
    PRIORITY ${PRIORITY}
    STACKSIZE ${STACKSIZE}
    MODULE ${MODULE}
    INCLUDE_DIRECTORIES ${INCDIR}
    SRCS ${MAINSRC} ${CSRCS}
  )
endif()
