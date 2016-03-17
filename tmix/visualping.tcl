#!/usr/bin/wish

set host [lindex $argv 0]
if { $host == "" } {
	puts "usage: [lindex $argv0] <target-host>"
	exit 1
}

proc line {cv x1 y1 x2 y2 {col green} {dashes {} } {ww 2}} {
	set ry1 [expr $::height - $y1]
	set ry2 [expr $::height - $y2]
	$cv create line $x1 $ry1 $x2 $ry2 -fill $col -dash $dashes -width $ww
}

proc clear_plot {cv} {
	global xmargin ymargin xm ym deltax tickw
	global prev_x prev_y cur_x cur_y	
	global step deltay n

	$cv delete "all"

	$cv create text $xmargin [expr $ymargin/2] -text "RTT (ms)"
	$cv create text [expr $::width/2] [expr $::height - $ymargin/2+$deltay] \
		-text "time (sec)"

	for {set  y 0} {$y<$ym} {incr y 50 } {

		set posy [expr $::height - $y - $ymargin ]
		set posx [expr $xmargin - $deltax]
		$cv create text $posx $posy -text "$y" 
		set xtic [expr $xmargin - $tickw]
		line $cv $xtic $posy $xmargin $posy 
		line $cv $xmargin $posy $xm $posy grey {2 2} 1		
	}

	for {set  x 0} {$x<$xm} {incr x [expr $step * 10]  } {

		set posy [expr $::height - $ymargin + $deltay]
		set posx [expr $xmargin + $x]
		$cv create text $posx $posy -text "$n" 
		set xtic [expr $xmargin - $tickw]
		set ytic [expr $ymargin - $tickw]
		line $cv $posx $ytic $posx $ymargin 

		incr n 10		
	}

	line $cv $xmargin $ymargin $xmargin $ym 
	line $cv $xmargin $ymargin $xm $ymargin 

	set cur_x $xmargin
	set prev_x $xmargin
}

set width    640
set height   400
set xmargin   40
set ymargin   50
set deltax    18
set deltay    18
set tickw      5
set n 0

set cur_x $xmargin
set cur_y $ymargin
set prev_x $xmargin
set prev_y $ymargin

set step 5

canvas  .cv -width $width -height $height  -bg white
pack .cv

set xm [expr $width - $xmargin]
set ym [expr $height - $ymargin]

clear_plot .cv


update 

while {true} {
	
	if [catch { 
		set smp [exec ping -c 1 $host -W 1 | grep icmp ] }
           ] { 
		puts "LOSS" 
	} else { 	
		set smp_txt [lindex [split $smp] 7]
		set rtt [lindex [split $smp_txt =] 1]

	}	

	set cur_x [expr $prev_x + $step]
	set cur_y [expr $rtt + $ymargin]
	line .cv $prev_x $prev_y $cur_x $cur_y
	update
	set prev_x $cur_x
	set prev_y $cur_y	

	if { $cur_x >= $xm } {
		clear_plot .cv
	}
 
	after 1000
}
