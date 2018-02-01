gcc -g -Iinclude/libobs -Iinclude/obs-frontend-api -shared pixel-switcher-filter.c libs/obs.lib libs/obs-frontend-api.lib -o pixel-switcher-filter.dll
gcc -g -Iinclude/obs-frontend-api -Iinclude/curl -Iinclude/libobs -shared bilibili-service.c libs/obs.lib libs/libcurl.lib libs/obs-frontend-api.lib -o bilibili-service.dll
