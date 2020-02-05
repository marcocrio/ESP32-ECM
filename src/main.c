//standar freeRTOS
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "freertos/queue.h"
#include "esp_int_wdt.h"
#include "esp_task_wdt.h"

//PWM
#include "driver/ledc.h"

//spiffs
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "spiffs_config.h"

//deilay in us
#include "rom/ets_sys.h"

//DAC ADC and PWM
#include <driver/ledc.h> 
#include <driver/adc.h> 
#include <driver/dac.h> 



static const char *TAG = "SPIFFS";

// ================== Global Variables Definition ==================//
   
 //-----------------VE Table-----------------//  
    //VE Headers
    int rpm[12]={0};
    int pre[16]={0};

    //VE[row][col]
    //VE [j][i]
    float VE[12][16]={0};

    float VE_Value =0; //the value retunrned from Interpolation
    
//-----------------Constans-----------------// 

    const float Vengine= 0.63; //VE Engine Constant
    const float UniGas= 0.28705; //Universal Gas Constant
    const int cylinder= 1; // **pending change to cylynders
    const float staticFlow= 10; // 10g/ms from Injector Datasheet
    const float openTime= 0.9 ;//The time in takes for the injector to be fully opened


//-----------------Sensors------------------//
    int TPS; //TPS adc readgins 
    float IAT = 311; //Air temperature in kelvin
    float TPSV;//TPS Voltage
    float pressure; //Barometric Pressure (hPa)
    float airmass, fuelmass, RPM, TPS_Percentage;
    float ckpPWM; //Ckp teeth PWM;
    float airmass; 
    float afr=14.7; //airfuel ratio (**needs to be improved, get from tables from book)  

    //Fuel Injector
    float freq;
    float injDuty;
    float injPulseTime;
    float injCycle;

//*****************************************************************************************//
//*****************************************************************************************//
//************************************* PWM  *****************************************//
//*****************************************************************************************//
//*****************************************************************************************//



void inj_pwm(void *pvParameter){

    while(1){

        ledc_set_duty(LEDC_HIGH_SPEED_MODE,LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE,LEDC_CHANNEL_0);
    }


}


void setUpPWM()
{   
    ledc_timer_config_t timerConfig;
    timerConfig.duty_resolution =LEDC_TIMER_10_BIT ;
    timerConfig.timer_num = LEDC_TIMER_0;
    timerConfig.freq_hz = ckpPWM*44;
    timerConfig.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_timer_config(&timerConfig);

    ledc_channel_config_t tChaConfig;
    tChaConfig.gpio_num = 0 ;
    tChaConfig.speed_mode = LEDC_HIGH_SPEED_MODE;
    tChaConfig.channel = LEDC_CHANNEL_0;
    tChaConfig.intr_type = LEDC_INTR_DISABLE;
    tChaConfig.timer_sel = LEDC_TIMER_0;
    tChaConfig.duty = TPS; // (2^10)*(%) 
    ledc_channel_config(&tChaConfig);
}



//*****************************************************************************************//
//*****************************************************************************************//
//********************************** READ VE TABLE ****************************************//
//*****************************************************************************************//
//*****************************************************************************************//


void rdfile(){
    char* token;

    int i,j=0;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");

    // FILE* f = fopen("/spiffs/hello.txt", "r");
    FILE* f = fopen("/spiffs/vetable.csv", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[150];


    while(fgets(line, sizeof(line), f)){
        i=0;
        

        token = strtok(line, ",");
        //printf("%s\n", token);
        if(! strcmp (token, "hPa")){
            token = strtok(NULL, ",");
            while (token != NULL) { 
                pre[i]=atoi(token);
                i++;
                token = strtok(NULL, ",");  
            }            
        }else if (! strcmp (token, "RPM")){
            token = strtok(NULL, ",");
            while (token != NULL) { 
                //printf("%s ",token);
                rpm[i]=atoi(token);
                i++;
                token = strtok(NULL, ","); 
            } 
        }else{
            while (token != NULL) { 
                //printf("%s ",token);
                VE[j][i]=atof(token);
                // printf("Row: %d | Col %d | Value: %f \n",j,i,VE[j][i]);
                token = strtok(NULL, ","); 
                i++;
            } 
            // printf("\n");
            j++;
        }   

    }
    fclose(f);


    //Print VE Table

    ESP_LOGI(TAG, "Pressure:");
    for(i=0; i< 16; i++){
        printf("%d ",pre[i]);
    }printf("\n");

    ESP_LOGI(TAG,"RPM:");
    for(i=0; i< 12; i++){
        printf("%d ",rpm[i]);
    }printf("\n");

    ESP_LOGI(TAG,"Volumetric Efficiency:");
    
    j=0;
    while(j<12){
        for(i=0; i< 16; i++){
            printf("%.2f ",VE[j][i]);
            
        }
        printf("\n");
        j++;
    }



    printf("\n"); 
    return;


}



//*****************************************************************************************//
//*****************************************************************************************//
//***************************** Virtual File System Init **********************************//
//*****************************************************************************************//
//*****************************************************************************************//


void vfsSetup(){
    ESP_LOGI(TAG, "\n\nInitializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    

    vTaskDelay(3000/portTICK_PERIOD_MS);

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    
}

//*****************************************************************************************//
//*****************************************************************************************//
//*********************************** INTERPOLATION ***************************************//
//*****************************************************************************************//
//*****************************************************************************************//



float power(float num){
    return num*num;
}


void interpolation(int hpa, int revs){

    // vTaskDelay(3000/portTICK_PERIOD_MS);
    // ESP_LOGI(TAG, "Initializing Interpoltion\n");
    // printf("Pressure: %d RPMS: %d\n",hpa,revs);
    int i=0,j=0;
    
    float x,y,yl,yh;
    float xl,xh;
    if(revs <= rpm[j]){
        xl=rpm[i];
        xh=0;
    }
    else{
        while(revs > rpm[j] && rpm[j]!=8000)j++;
        xl=rpm[i-1];
        xh=rpm[i];
    };
    
    if(hpa <= pre[i]){
        yl=pre[i];
        yh=0;
    }
    else{
        while(hpa > pre[i] && pre[i]!=1050)i++;
        yl=pre[i-1];
        yh=pre[i];
    }

    // printf("i:%d | j:%d\n",i,j);
    // printf("%.2f   %.2f\n",VE[j-1][i-1], VE[j-1][i]);
    // printf("%.2f   %.2f\n",VE[j][i-1], VE[j][i]);



    float distance = (hpa - yl)/(yh-yl);
    // printf("Distance: %.2f\n",distance);

    float distdiff = VE[j-1][i]-VE[j-1][i-1];
    // printf("Distance Difference: %.2f\n",distdiff);

    x = VE[j-1][i-1] + ( distdiff * distance );

    y = VE[j-1][i-1] + ( ( VE[j][i-1] - VE[j-1][i-1] ) / ( VE[j-1][i] - VE[j-1][i-1]) ) * ( x - VE[j-1][i-1] );

    // printf("x: %.4f | y: %.4f\n",x,y);



    float VolEff = VE[j-1][i-1]+ sqrt(  power( x-VE[j-1][i-1] ) + power(y-VE[j-1][i-1])   );

    // printf("Volumetric Efficiency: %.2f\n",VolEff);

    VE_Value=VolEff;


    //printf("i:%d | x1: %d x2: %d\n",i,xl,xh);
    //printf("j:%d | y1: %d y2: %d\n",j,yl,yh);
    
    // printf("\n");

    return;

};



//*****************************************************************************************//
//*****************************************************************************************//
//******************************* Readings Collection *************************************//
//*****************************************************************************************//
//*****************************************************************************************//


void main_Readings(void *pvParameter)
{

    dac_output_enable(DAC_CHANNEL_1);//pressure output
    dac_output_enable(DAC_CHANNEL_2);//RPMS output
    int i; 
    esp_task_wdt_add(NULL);

    while(1)
    {
       
        ESP_LOGI(TAG, "Readings\n");
       //Get the read from TPS
       TPS =0; 
       for(i=0 ; i<1000; i++)
        { 
            TPS += adc1_get_raw(ADC1_CHANNEL_4); 
        } 
        TPS= TPS/ 1000;  
        printf("TPS ADC: %d \n",TPS);
        ets_delay_us(10); //sincronizes main reading task and CKP signal creation
        
        //Get TPS Voltage
        TPSV = TPS * 0.00449658;  
        printf("TPS Voltage: %.4f (V)\n",TPSV); 

        //Get TPS Percentage
        TPS_Percentage = (TPSV/4.6)*100; //originally voltage/4.6*100 changed to pressure/4.6 
        printf("TPS%%: %.4f%%\n",TPS_Percentage); 


        //Get MAP Reading on TPS% relation
        if (TPS_Percentage<11)
        pressure = -20*(TPS_Percentage)+1022;
        else
        pressure = -2.15*(TPS_Percentage-20)+755;


        printf("pressure: %.4f (kPa)\n",pressure); 
        dac_output_voltage(DAC_CHANNEL_1, (pressure-1022)*(-0.58));//kpascual 
        
        
        //Get RPM Based on TPS% relation
        if(TPS_Percentage<10)
            RPM = 100*(TPS_Percentage)+1200;
        else if (TPS_Percentage > 40) //added for RPMS 
            RPM= 10*(TPS_Percentage-60)+8000;
        else
            RPM= 200*(TPS_Percentage-10)+2000; 

        printf("RPM: %.4f\n",RPM);  
        
        dac_output_voltage(DAC_CHANNEL_2, (RPM-1200)*0.0375);//RPMS output


        //Get CKP PWM based on RPM Relation
        if(RPM == 0) 
            ckpPWM = (60/(44*1))*1000000;
        else 
            ckpPWM = (60/(44*RPM))*1000000;

        printf("ckpPWM: %.4f us\n",ckpPWM);

       

        //Volumetric Efficiency 
        interpolation(pressure,RPM); //Gets the exact VE value
        printf("Volumetric Efficiency: %.2f\n",VE_Value);


        //Airmass
        airmass = (Vengine*VE_Value*pressure)/(UniGas*IAT*cylinder); 
        printf("airmass: %.4f (g/cyl)\n",airmass);

        //Fuelmass
        fuelmass = (airmass)/(afr); 
        printf("fuelmass: %.4f (g/cyl)\n\n",fuelmass); 


        //Fuel injector
        freq=ckpPWM*44;
        // injCycle = 60/RPM;
        // injPulseTime= (fuelmass/staticFlow + openTime)*1000; //the open time
        injDuty = TPS;

        printf("Frequency: %.4f\n",freq);
        // printf("Injecor Pulse Time: %.4fms\n",injPulseTime);
        printf("Injector Duty Cyle: %.4f\n",injDuty);

        setUpPWM();


        //Sincronization delay
        ets_delay_us(10); //sincronizes main reading task and CKP signal creation
        esp_task_wdt_reset();

    }
}



//*****************************************************************************************//
//*****************************************************************************************//
//*********************************** CKP Signal ******************************************//
//*****************************************************************************************//
//*****************************************************************************************//


void ckp_signal(void *pvParameter)
{    
    ets_delay_us(10); // sincronization with Main Readings task
    
    gpio_pad_select_gpio(2);
    esp_task_wdt_add(NULL);// subscription to WDT

    //int on=0;
    //int i;
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(2, GPIO_MODE_OUTPUT);
   
    while(1) 
    {
        


        esp_task_wdt_reset();
    }
};







//*****************************************************************************************//
//*****************************************************************************************//
//************************************* ADC Setup *****************************************//
//*****************************************************************************************//
//*****************************************************************************************//

void setADC() 
{  
    adc1_config_width(ADC_WIDTH_BIT_10); 
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11 ); 
} 

//*****************************************************************************************//
//*****************************************************************************************//
//*************************************** DEBUG ********************************************//
//*****************************************************************************************//
//*****************************************************************************************//



void clr_scrn(void *pvParameter)
{

    long i=0;
    int b=0;
    esp_task_wdt_add(NULL);
    while(1){

        
        if(i>100000){
            b=b+10;
            printf("\033[2J");
            printf("\033[H");
            printf("%d\n",b);
            i=0;
        }else{
            i++;
        }



        esp_task_wdt_reset();

    }

}


//*****************************************************************************************//
//*****************************************************************************************//
//*************************************** MAIN ********************************************//
//*****************************************************************************************//
//*****************************************************************************************//


void app_main(void){

    esp_task_wdt_init(30,0);// Watchdog timer settings it lasts 30 minutes and the 0 indicates that there will not be error.
    setADC(); //Set up ADC at 10 bit
    vfsSetup(); //initializes Virtual File System
    rdfile(); //Reads VE table
    setUpPWM(); //Setsup up PWM

    xTaskCreate(&main_Readings, "main_Readings", 2048, NULL, 5, NULL);
    xTaskCreate(&ckp_signal, "ckp_signal", 2048, NULL, 5, NULL); 
    xTaskCreate(&inj_pwm, "inj_pwm", 2048, NULL, 5, NULL); 
    //xTaskCreate(&read_file, "read_file", 4096, NULL, 6, NULL);

    //xTaskCreate(&clr_scrn, "clr_srcn", 2048, NULL, 6, NULL); 

    return;

}
