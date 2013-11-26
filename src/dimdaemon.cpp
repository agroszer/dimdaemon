#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <fstream>
#include <iostream>
#include <math.h>
#include <X11/extensions/scrnsaver.h>
#include <sys/stat.h>
#include <libconfig.h++>

using namespace std;
using namespace libconfig;

int getmaxbright();
int getbright();
int setdim(int percent);
int setreldim(int brightness, int percent);
void setbright(int new_brightness);
void turnoffdisplay();
int onbattery();
void parseconfig(string filename);
void setenviroment();

//global variables
bool relative = false;
bool bat_active = true;
bool ac_active = false;
int idletimedimbat = 3000;
int idletimedimac = 5000;
int idletimeturnoffbat = 10000;
int idletimeturnoffac = 20000;
int dimpercentbat = 50;
int dimpercentac = 80;
int undimpercentbat = 100;
int undimpercentac = 100;
string userhome = "";
string displayname = "";
string powersupplyname = "";

// look at the maximum brightness level the acpi system knows of
int getmaxbright() {
	string path = "/sys/class/backlight/" + displayname + "/max_brightness";
	fstream max_bright_file(path.c_str(),fstream::in);
	int max_bright;
	max_bright_file >> max_bright;
	return max_bright;
}

// look at the actual brightness level
int getbright() {
	string path = "/sys/class/backlight/" + displayname + "/brightness";
	fstream bright_file(path.c_str(),fstream::in);
	int bright;
	bright_file >> bright;
	return bright;
}

// as maximum brightness levels might not be the same on all systems we need to calculate the new level for a given percentage
int setdim(int percent) {
	float dimpercent = (float) percent / 100.0;
	int dimlevel = round(dimpercent * getmaxbright());
	setbright(dimlevel);
	return dimlevel;
}

//  calculate the new relative level
int setreldim(int brightness, int percent) {
	float dimpercent = (float) percent / 100.0;
	int dimlevel = round(dimpercent * brightness);
	setbright(dimlevel);
	return dimlevel;
}

// set new brightness level
void setbright(int new_brightness) {
	string path = "/sys/class/backlight/" + displayname + "/brightness";
	fstream bright_file(path.c_str(),fstream::out);
	bright_file << new_brightness << endl;
	bright_file.close();
}

// turn the display backlight off
void turnoffdisplay() {
	Display *display = XOpenDisplay(0);
	sleep(1);
	DPMSForceLevel(display, DPMSModeOff);
	XCloseDisplay (display);
}

// check if the laptop runs on battery
int online() {
	string path = "/sys/class/power_supply/" + powersupplyname + "/online";
	fstream online_file(path.c_str(),fstream::in);
	int online;
	online_file >> online;
	return online;
}

 // read config file using libconfig
void parseconfig(string filename) {
	Config config;
	config.readFile(filename.c_str());

	config.lookupValue("enviroment.userhome", userhome);
	config.lookupValue("enviroment.displayname", displayname);
	config.lookupValue("enviroment.powersupplyname", powersupplyname);
	config.lookupValue("enviroment.relative", relative);

	config.lookupValue("ac.active", ac_active);
	config.lookupValue("ac.undimpercent", undimpercentac);
	config.lookupValue("ac.dimtime", idletimedimac);
	idletimedimac *= 1000;
	config.lookupValue("ac.dimpercent", dimpercentac);
	config.lookupValue("ac.turnofftime", idletimeturnoffac);
	idletimeturnoffac *= 1000;

	config.lookupValue("battery.active", bat_active);
	config.lookupValue("battery.undimpercent", undimpercentbat);
	config.lookupValue("battery.dimtime", idletimedimbat);
	idletimedimbat *= 1000;
	config.lookupValue("battery.dimpercent", dimpercentbat);
	config.lookupValue("battery.turnofftime", idletimeturnoffbat);
	idletimeturnoffbat *= 1000;
}

void setenviroment() {
	string xauthortiypath = userhome + "/.Xauthority";
	setenv("XAUTHORITY", xauthortiypath.c_str(),1);
	setenv("DISPLAY", ":0", 1);
}

int main() {
	parseconfig("/etc/dimdaemon.conf");

	int oldbrightness = 0;
	if (relative)
		oldbrightness = getbright();

	int dim_state = -1; // 0 onbat_nodim; 1 onbat_dim; 2 onbat_off; 3 onac_nodim; 4 onac_dim; 5 onac_off;

	pid_t pid;
	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}
	umask(0);

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	// check if we have a running xserver
	while(1) {
		setenviroment();
		Display *temp_display = XOpenDisplay(NULL);
		if(temp_display != 0) {
			XCloseDisplay (temp_display);
			// turn the display off if the specified time is expired
			while(1) {
				XScreenSaverInfo *info = XScreenSaverAllocInfo();
				Display *display = XOpenDisplay(NULL);
				XScreenSaverQueryInfo(display, DefaultRootWindow(display), info);
				XCloseDisplay (display);
				if(relative) {
					if((online() == 0) && (bat_active == true)) {
						if(info->idle < idletimedimbat) {
							if(dim_state != 0) {
								setbright(oldbrightness);
								dim_state = 0;
							}
						}
						else if((info->idle > idletimedimbat) && (info->idle < idletimeturnoffbat)) {
							if(dim_state != 1) {
								oldbrightness = getbright();
								setreldim(oldbrightness, dimpercentbat);
								dim_state = 1;
							}
						}
						else if(info->idle > idletimeturnoffbat) {
							if(dim_state != 2) {
								turnoffdisplay();
								setbright(oldbrightness);
								dim_state = 2;
							}
						}
					}
					if((online() == 1) && (ac_active == true)) {
						if(info->idle < idletimedimac) {
							if(dim_state != 3) {
								setbright(oldbrightness);
								dim_state = 3;
							}
						}
						else if((info->idle > idletimedimac) && (info->idle < idletimeturnoffac)) {
							if(dim_state != 4) {
								oldbrightness = getbright();
								setreldim(oldbrightness, dimpercentac);
								dim_state = 4;
							}
						}
						else if(info->idle > idletimeturnoffac) {
							if(dim_state != 5) {
								turnoffdisplay();
								setbright(oldbrightness);
								dim_state = 5;
							}
						}
					}
				}
				else {
					// use battery values if we are on battery
					if((online() == 0) && (bat_active == true)) {
						if(info->idle < idletimedimbat) {
							if(dim_state != 0) {
								setdim(undimpercentbat);
								dim_state = 0;
							}
						}
						else if((info->idle > idletimedimbat) && (info->idle < idletimeturnoffbat)) {
							if(dim_state != 1) {
								setdim(dimpercentbat);
								dim_state = 1;
							}
						}
						else if(info->idle > idletimeturnoffbat) {
							if(dim_state != 2) {
								turnoffdisplay();
								setdim(undimpercentbat);
								dim_state = 2;
							}
						}
					}
					// use ac values if we aren't on battery
					if((online() == 1) && (ac_active == true)) {
						if(info->idle < idletimedimac) {
							if(dim_state != 3) {
								setdim(undimpercentac);
								dim_state = 3;
							}
						}
						else if((info->idle > idletimedimac) && (info->idle < idletimeturnoffac)) {
							if(dim_state != 4) {
								setdim(dimpercentac);
								dim_state = 4;
							}
						}
						else if(info->idle > idletimeturnoffac) {
							if(dim_state != 5) {
								turnoffdisplay();
								setdim(undimpercentac);
								dim_state = 5;
							}
						}
					}
				}
				XFree(info);
				sleep(1);
			}
		}
		sleep(5);
	}
}
