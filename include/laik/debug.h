/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LAIK_DEBUG_H_
#define _LAIK_DEBUG_H_

#ifndef _LAIK_H_
#error "include laik.h instead"
#endif

#include <stdbool.h>

void laik_log_IntList(int len, int* list);
void laik_log_PrettyInt(uint64_t v);
void laik_log_Space(Laik_Space* spc);
void laik_log_Index(int dims, Laik_Index* idx);
void laik_log_Slice(int dims, Laik_Slice* slc);
void laik_log_Reduction(Laik_ReductionOperation op);
void laik_log_DataFlow(Laik_DataFlow flow);
void laik_log_Transition(Laik_Transition* t);
void laik_log_BorderArray(Laik_BorderArray* ba);
void laik_log_SwitchStat(Laik_SwitchStat* ss);

int laik_getIntListStr(char* s, int len, int* list);
int laik_getSpaceStr(char* s, Laik_Space* spc);
int laik_getIndexStr(char* s, int dims, Laik_Index* idx);
int laik_getSliceStr(char* s, int dims, Laik_Slice* slc);
int laik_getReductionStr(char* s, Laik_ReductionOperation op);
int laik_getDataFlowStr(char* s, Laik_DataFlow flow);
int laik_getTransitionStr(char* s, Laik_Transition* t);
int laik_getBorderArrayStr(char* s, Laik_BorderArray* ba);


#endif // _LAIK_DEBUG_H_
