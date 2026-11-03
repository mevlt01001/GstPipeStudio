#!/usr/bin/env bats

GST_SCRIPT="gstpipestudio/scripts/check_gst.sh"

@test "GStremer version >= 1.16" {
    run bash "$GST_SCRIPT"
    [ "$status" -eq 0 ]
}
