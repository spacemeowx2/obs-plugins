gcc -g -Iobs-frontend-api -Iinclude/libobs -Iinclude/obs-frontend-api -shared pixel-switcher-filter.c libs/obs.lib libs/obs-frontend-api.lib -o pixel-switcher-filter.dll

gcc -g -Iinclude -Iinclude/libobs -shared bilibili-service.c libs/obs.lib libs/libcurl.lib -o bilibili-service.dll
