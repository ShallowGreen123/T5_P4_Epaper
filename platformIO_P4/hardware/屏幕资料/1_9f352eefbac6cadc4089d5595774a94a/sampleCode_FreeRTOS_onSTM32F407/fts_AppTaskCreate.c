/*
 * Privileged & confidential  All Rights/Copyright Reserved by FocalTech.
 *       ** Source code released bellows and hereby must be retained as
 * FocalTech's copyright and with the following disclaimer accepted by
 * Receiver.
 *
 * "THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL THE FOCALTECH'S AND ITS AFFILIATES'DIRECTORS AND OFFICERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."
 */
/* FreeRTOS头文件 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


/* 开发板硬件bsp头文件 */
#include "timers.h"

#include "fts_AppTaskCreate.h"
#include "focaltech_core.h"
/**************************** 任务句柄 ********************************/
/*
 * 任务句柄是一个指针，用于指向一个任务，当任务创建好之后，它就具有了一个任务句柄
 * 以后我们要想操作这个任务都需要通过这个任务句柄，如果是自身的任务操作自己，那么
 * 这个句柄可以为NULL。
 */
TaskHandle_t AppTaskCreate_Handle = NULL;/* 创建任务句柄 */
static TaskHandle_t Upgrade_Task_Handle = NULL;/* upgrate任务句柄 */
TaskHandle_t FTS_Task_Handle = NULL;
/********************************** 内核对象句柄 *********************************/
/*
 * 信号量，消息队列，事件标志组，软件定时器这些都属于内核的对象，要想使用这些内核
 * 对象，必须先创建，创建成功之后会返回一个相应的句柄。实际上就是一个指针，后续我
 * 们就可以通过这个句柄操作这些内核对象。
 *
 * 内核对象说白了就是一种全局的数据结构，通过这些数据结构我们可以实现任务间的通信，
 * 任务间的事件同步等各种功能。至于这些功能的实现我们是通过调用这些内核对象的函数
 * 来完成的
 *
 */

/******************************* 全局变量声明 ************************************/
/*
 * 当我们在写应用程序的时候，可能需要用到一些全局变量。
 */


QueueHandle_t FTS_Int_Queue =NULL;//中断队列handle

/*
 * 当我们在写应用程序的时候，可能需要用到一些宏定义。
 */
#define  QUEUE_LEN    4   /* 队列的长度，最大可包含多少个消息 */
#define  QUEUE_SIZE   4   /* 队列中每个消息大小（字节） */
/*
*************************************************************************
*                             函数声明
*************************************************************************
*/

static void Upgrade_Task(void* pvParameters);/* Upgrade_Task任务实现 */
static void FTS_Int_Task(void* parameter);/* FTS_Int_Task 任务实现 */

TimerHandle_t xTimer_t = NULL;/*用于创建timer任务*/

extern int fts_fwupg_auto_upgrade(void);


/**********************************************************************
  * @ 函数名  ： FTS_INT_Task
  * @ 功能说明： FTS_INT_Task任务主体
  * @ 参数    ：
  * @ 返回值  ： 无
  ********************************************************************/
static void fts_esdCheckCallback( TimerHandle_t pxTimer )
{
    // Optionally do something if the pxTimer parameter is NULL.
    configASSERT( pxTimer );

    // esd 检查
    fts_timer_interrupt_handler();

}

/**********************************************************************
  * @ 函数名  ： FTS_INT_Task
  * @ 功能说明： FTS_INT_Task任务主体
  * @ 参数    ：
  * @ 返回值  ： 无
  ********************************************************************/
static void FTS_Int_Task(void* parameter)
{
    BaseType_t xReturn = pdPASS;/* 定义一个创建信息返回值，默认为pdPASS */
    uint32_t r_queue;	/* 定义一个接收消息的变量 */
    while (1)
    {
        /* 队列读取（接收），等待时间为一直等待 */
        xReturn = xQueueReceive( FTS_Int_Queue,    /* 消息队列的句柄 */
                                 &r_queue,      /* 发送的消息内容 */
                                 portMAX_DELAY); /* 等待时间 一直等 */

        if(pdPASS == xReturn)
        {
            //确保是否产生了EXTI Line中断
            uint32_t ulReturn;
            /* 进入临界段，临界段可以嵌套 */
            ulReturn = taskENTER_CRITICAL_FROM_ISR();

            //读取报点数据
            fts_gpio_interrupt_handler();


            /* 退出临界段 */
            taskEXIT_CRITICAL_FROM_ISR( ulReturn );
        }
        else
        {
            printf("数据接收出错\n");
        }

    }
}
/**********************************************************************
  * @ 函数名  ： Test_Task
  * @ 功能说明： Test_Task任务主体
  * @ 参数    ：
  * @ 返回值  ： 无
  ********************************************************************/
static void Upgrade_Task(void* parameter)
{
    uint32_t ulReturn;

    if(!fts_check_id()) {
        xTimerStop(xTimer_t,0);
        /* 进入临界段，临界段可以嵌套 */
        ulReturn = taskENTER_CRITICAL_FROM_ISR();
			
        vTaskSuspend(FTS_Task_Handle);
			
        //升级固件，若版本相同则跳过升级
        fts_fwupg_auto_upgrade();
			
        vTaskResume(FTS_Task_Handle);/* 恢复中断任务！ */


        /* 退出临界段 */
        taskEXIT_CRITICAL_FROM_ISR( ulReturn );

        xTimerStart(xTimer_t,0);
    }else{
			    //获取ic失败，停止timer以及挂起FTS_Task
					xTimerStop(xTimer_t,0);
					vTaskSuspend(FTS_Task_Handle);
		}

    while (1)
    {

        vTaskDelay(20);/* 延时20个tick */
    }
}


/***********************************************************************
  * @ 函数名  ： fts_AppTaskCreate
  * @ 功能说明： 为了方便管理，所有的任务创建函数都放在这个函数里面
  * @ 参数    ： 无
  * @ 返回值  ： 无
  **********************************************************************/
void fts_AppTaskCreate(void)
{
    BaseType_t xReturn = pdPASS;/* 定义一个创建信息返回值，默认为pdPASS */

    taskENTER_CRITICAL();           //进入临界区

    /* 创建FTS_INT_Queue */
    FTS_Int_Queue = xQueueCreate((UBaseType_t ) QUEUE_LEN,/* 消息队列的长度 */
                                 (UBaseType_t ) QUEUE_SIZE);/* 消息的大小 */

    if(NULL != FTS_Int_Queue)
        printf("FTS_Int_Queue消息队列创建成功!\n");


    /* 创建Upgrade_Task任务 */
    xReturn = xTaskCreate((TaskFunction_t )Upgrade_Task,  /* 任务入口函数 */
                          (const char*    )"Upgrade_Task",/* 任务名字 */
                          (uint16_t       )512,  /* 任务栈大小 */
                          (void*          )NULL,/* 任务入口函数参数 */
                          (UBaseType_t    )3, /* 任务的优先级 */
                          (TaskHandle_t*  )&Upgrade_Task_Handle);/* 任务控制块指针 */
    if(pdPASS == xReturn)
        printf("创建Upgrade_Task任务成功!\r\n");


    /* 创建LED_Task任务 */
    xReturn = xTaskCreate((TaskFunction_t )FTS_Int_Task, /* 任务入口函数 */
                          (const char*    )"FTS_Int_Task",/* 任务名字 */
                          (uint16_t       )512,   /* 任务栈大小 */
                          (void*          )NULL,	/* 任务入口函数参数 */
                          (UBaseType_t    )2,	    /* 任务的优先级 */
                          (TaskHandle_t*  )&FTS_Task_Handle);/* 任务控制块指针 */
    if(pdPASS == xReturn)
        printf("创建FTS_Int_Task任务成功!\n");


#if 1
    xTimer_t = xTimerCreate(	(const char * ) "esd check task",
                                ( TickType_t) 1000,
                                ( UBaseType_t) pdTRUE,
                                (void * ) 4,
                                (TimerCallbackFunction_t) fts_esdCheckCallback );

    if(xTimer_t != NULL) {
        if(xTimerStart(xTimer_t,0) == pdPASS)
            printf("创建esd check task任务成功!\r\n");
    }
#endif

    vTaskDelete(AppTaskCreate_Handle); //删除AppTaskCreate任务

    taskEXIT_CRITICAL();            //退出临界区

}

