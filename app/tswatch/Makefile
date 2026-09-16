#
# Copyright (C) 2022 Xiaomi Corporation
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

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_TSWATCH), y)
PROGNAME = tswatch
PRIORITY = 100
STACKSIZE = 32768
MODULE = $(CONFIG_TSWATCH)

CSRCS = $(wildcard main_page/src/*.c) $(wildcard main_page/res/images/*.c)
CSRCS += $(wildcard applist/src/*.c) $(wildcard applist/res/*.c)
CSRCS += $(wildcard notification/src/*.c) $(wildcard notification/res/*.c)
# CSRCS += $(wildcard launcher/src/*.c) $(wildcard launcher/res/*.c)
CSRCS += $(wildcard timer/src/*.c) $(wildcard timer/res/*.c)
CSRCS += $(wildcard settings/src/*.c) $(wildcard settings/res/*.c)
CSRCS += $(wildcard clock/src/*.c) $(wildcard clock/res/*.c)
CSRCS += $(wildcard calendar/src/*.c) $(wildcard calendar/res/*.c)
CSRCS += $(wildcard stopwatch/src/*.c) $(wildcard stopwatch/res/*.c)
CSRCS += $(wildcard sleep/src/*.c) $(wildcard sleep/res/*.c)
CSRCS += $(wildcard heart/src/*.c) $(wildcard heart/res/*.c)
CSRCS += $(wildcard exercise/src/*.c) $(wildcard exercise/res/*.c)
CSRCS += $(wildcard utils/src/*.c)  $(wildcard utils/res/*.c)
CSRCS += $(wildcard weather/src/*.c) $(wildcard weather/res/images/*.c)
#CSRCS += $(wildcard quicks/src/*.c) $(wildcard quicks/res/*.c)
#CSRCS += $(wildcard quick_balloon/src/*.c) $(wildcard quick_balloon/res/*.c)
CSRCS += $(wildcard music_demo/src/*.c) $(wildcard music_demo/res/*.c)
CSRCS += $(wildcard music_list/src/*.c) $(wildcard music_list/res/*.c)
CSRCS += $(wildcard xiaozhi_voice/*.c)

MAINSRC = watch_main.c
$(info 当前路径是：$(shell pwd))
$(warning $(CSRCS))

CFLAGS +=-Imain_page/include
VPATH  += main_page/src main_page/res/images

CFLAGS +=-Iapplist/include
VPATH  += applist/src applist/res

CFLAGS +=-Inotification/include
VPATH  += notification/src notification/res

# CFLAGS +=-Ilauncher/include
# VPATH  += launcher/src launcher/res

CFLAGS +=-Itimer/include
VPATH  += timer/src timer/res

CFLAGS +=-Isettings/include
VPATH  += settings/src settings/res

CFLAGS +=-Icalendar/include
VPATH  += calendar/src calendar/res

CFLAGS +=-Istopwatch/include
VPATH  += stopwatch/src stopwatch/res

CFLAGS +=-Isleep/include
VPATH  += sleep/src sleep/res

CFLAGS +=-Iheart/include
VPATH  += heart/src heart/res

CFLAGS +=-Iexercise/include
VPATH  += exercise/src exercise/res

CFLAGS +=-Iutils/include
VPATH  += utils/src utils/res

CFLAGS +=-Iclock/include
VPATH  += clock/src clock/res

CFLAGS +=-Iweather/include
VPATH  += weather/src weather/res/images

# CFLAGS +=-Iquicks/include
# VPATH  += quicks/src quicks/res

# CFLAGS +=-Iquick_balloon/include
# VPATH  += quick_balloon/src quick_balloon/res

CFLAGS +=-Imusic_demo/include
VPATH  += music_demo/src music_demo/res

CFLAGS +=-Imusic_list/include
VPATH  += music_list/src music_list/res

CFLAGS +=-Ixiaozhi_voice
CFLAGS +=-I$(APPDIR)/../external/opus/opus/include
VPATH  += xiaozhi_voice
endif

#cp -r /res /data

include $(APPDIR)/Application.mk
