#!/usr/bin/env python3

"""
Script for generating default wsmod config from a vanilla game's files
warning: bad
"""

from pathlib import Path
import struct
from collections import namedtuple
import logging
import sys
import json

VANILLA_ROOT_PATH = Path(
    "/mnt/c/Users/ComplexPlane/Documents/projects/romhack/smb2imm/files"
)

CourseCommand = namedtuple("CourseCommand", ["opcode", "type", "value"])

# CMD opcodes
CMD_IF = 0
CMD_THEN = 1
CMD_FLOOR = 2
CMD_COURSE_END = 3

# CMD_IF conditions
IF_FLOOR_CLEAR = 0
IF_GOAL_TYPE = 2

# CMD_THEN actions
THEN_JUMP_FLOOR = 0
THEN_END_COURSE = 2

# CMD_FLOOR value types
FLOOR_STAGE_ID = 0
FLOOR_TIME = 1


def parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, start, count):
    cmds: list[CourseCommand] = []
    course_cmd_size = 0x1C

    for i in range(count):
        course_cmd = CourseCommand._make(struct.unpack_from(
            ">BBxxI20x",
            mainloop_buffer,
            start + i * course_cmd_size,
        ))
        cmds.append(course_cmd)

    # Course commands to stage infos
    cm_stage_infos = []
    stage_id = 0
    stage_time = 60 * 60
    blue_jump = None
    green_jump = None
    red_jump = None
    last_goal_type = None
    first = True
    finished = False

    for cmd in cmds:
        if cmd.opcode == CMD_FLOOR:
            if cmd.type == FLOOR_STAGE_ID:
                if not first:
                    if blue_jump is None:
                        logging.error("Invalid blue goal jump")
                        sys.exit(1)

                    cm_stage_infos.append({
                        "stage_id": stage_id,
                        "name": stgname_lines[stage_id],
                        "theme_id": 0, # TODO
                        "music_id": 0, # TODO
                        "time_limit": float(stage_time / 60),                      

                        "blue_goal_jump": blue_jump,
                        "green_goal_jump": green_jump if green_jump is not None else blue_jump,
                        "red_goal_jump": red_jump if red_jump is not None else blue_jump,
                        "is_bonus_stage": stage_id in bonus_stage_ids,
                    })
                    stage_id = 0
                    stage_time = 60 * 60
                    blue_jump = None
                    green_jump = None
                    red_jump = None
                    last_goal_type = None

                stage_id = cmd.value
                first = False

            elif cmd.type == FLOOR_TIME:
                stage_time = cmd.value
            else:
                logging.error(f"Invalid CMD_FLOOR opcode type: {cmd.type}")
                sys.exit(1)

        elif cmd.opcode == CMD_IF:
            if cmd.type == IF_FLOOR_CLEAR:
                last_goal_type = None
            elif cmd.type == IF_GOAL_TYPE:
                last_goal_type = cmd.value
            else:
                logging.error(f"Invalid CMD_IF opcode type: {cmd.type}")
                sys.exit(1)

        elif cmd.opcode == CMD_THEN:
            if cmd.type == THEN_JUMP_FLOOR:
                if last_goal_type is None:
                    if blue_jump is None:
                        blue_jump = cmd.value
                    if green_jump is None:
                        green_jump = cmd.value
                    if red_jump is None:
                        red_jump = cmd.value
                elif last_goal_type == 0:
                    blue_jump = cmd.value
                elif last_goal_type == 1:
                    green_jump = cmd.value
                elif last_goal_type == 2:
                    red_jump = cmd.value
                else:
                    logging.error(f"Invalid last goal type: {last_goal_type}")
                    sys.exit(1)
            elif cmd.type == THEN_END_COURSE:
                # Jumps are irrelevant, this is end of difficulty
                blue_jump = 1
                green_jump = 1
                red_jump = 1
            else:
                logging.error(f"Invalid CMD_THEN opcode type: {cmd.type}")
                sys.exit(1)

        elif cmd.opcode == CMD_COURSE_END:
            if blue_jump is None:
                logging.error("Invalid blue goal jump")
                sys.exit(1)
            cm_stage_infos.append({
                "stage_id": stage_id,
                "name": stgname_lines[stage_id],
                "theme_id": 0, # TODO
                "music_id": 0, # TODO
                "time_limit": float(stage_time / 60),                      

                "blue_goal_jump": blue_jump,
                "green_goal_jump": green_jump if green_jump is not None else blue_jump,
                "red_goal_jump": red_jump if red_jump is not None else blue_jump,
                "is_bonus_stage": stage_id in bonus_stage_ids,
            })
            finished = True

        else:
            logging.error(f"Invalid opcode: {cmd.opcode}")
            sys.exit(1)

    if not finished:
        logging.error("Course command list ended early")
        sys.exit(1)

    return cm_stage_infos


def main():
    with open(VANILLA_ROOT_PATH / "mkb2.main_loop.rel", "rb") as f:
        mainloop_buffer = f.read()
    with open(VANILLA_ROOT_PATH / "stgname" / "usa.str", "r") as f:
        stgname_lines = [s.strip() for s in f.readlines()]

    bonus_stage_ids = struct.unpack_from(">9i", mainloop_buffer, 0x00176118)

    # Parse challenge mode entries
    beginner = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x002075B0, 31)
    advanced = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x00207914, 120)
    expert = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x00208634, 208)
    beginner_extra = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x00209cf4, 35)
    advanced_extra = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x0020a0c8, 32)
    expert_extra = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x0020a448, 42)
    master = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x0020a8e0, 35)
    master_extra = parse_cm_course(mainloop_buffer, stgname_lines, bonus_stage_ids, 0x0020acb4, 50)
    cm_layout = {
        "beginner": beginner,
        "beginner_extra": beginner_extra,
        "advanced": advanced,
        "advanced_extra": advanced_extra,
        "expert": expert,
        "expert_extra": expert_extra,
        "master": master,
        "master_extra": master_extra,
    }

    print(json.dumps(cm_layout, indent=4))

if __name__ == "__main__":
    main()
