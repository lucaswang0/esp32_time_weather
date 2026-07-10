#include "TempPage.h"
#include "DisplayManager.h"
#include "WeatherManager.h"
#include "AHT20BMP280Sensor.h"
#include "WiFiManager.h"

TempPage::TempPage(DisplayManager& disp, WeatherManager& weather,
                   AHT20BMP280Sensor& aht20, WiFiManager& wifi)
    : display(disp), weather(weather), aht20(aht20), wifi(wifi) {}

void TempPage::onEnter() {
    Serial.println("[TempPage] onEnter");
    display.clearScreen();
    display.resetCache();
}

void TempPage::update() {
    bool forecastValid = !weather.getForecast(0).date.isEmpty();
    display.displayWeather(
        weather.getCity(),
        weather.getWeatherText(),
        weather.getTemperature(),
        weather.getWeatherCode(),
        forecastValid
    );

    const DailyForecast& todayForecast = weather.getForecast(0);
    display.displayForecast(
        todayForecast.tempMin,
        todayForecast.tempMax,
        todayForecast.textDay.length() > 0 ? weather.getWeatherCode() : ""
    );

    display.displaySunMoon(todayForecast.sunrise, todayForecast.sunset);

    if (forecastValid && weather.getTemperature().length() > 0) {
        float temp = weather.getTemperature().toFloat();
        float humi = todayForecast.humidity.length() > 0 ? todayForecast.humidity.toFloat() : aht20.getHumidity();
        float apparent = display.calcApparentTemperature(temp, humi);
        display.displayApparentTemp(apparent, humi);
    }

    display.displayIndoorTemp(aht20.getTemperature(), aht20.isValid());
    display.displayIndoorHumidity(aht20.getHumidity(), aht20.isValid());
    display.displayWiFiStatus(wifi.isConnected());
}
