#include "jmp_gcode_analysis.h"
#include "jmp_command_execution.h"
#include "jmp_param_state.h"
#include "jmp_gui.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "croutine.h"
#include "string.h"
#include <stdlib.h>
#include "jmp_print_time.h"


/***********************************************************************************************************
该模块为gcode解析模块，用于解析从存储中读取的gcode
***********************************************************************************************************/


//gcode缓冲区
char jmp_gcode_buff[GCODE_BUFF_SUM];
u32 jmp_gcode_buff_sp;
u32 jmp_gcode_buff_ep;
u32 jmp_gcode_buff_full;

//行gcode缓冲区
char jmp_gcode_line_buff[GCODE_LINE_BUFF_SUM];
u32 jmp_gcode_line_buff_sum;

//gcode指令参数缓冲区
char jmp_gcode_param_buff[GCODE_PARAM_SUM][GCODE_PARAM_LENGTH];
u32 jmp_gcode_param_buff_sum;

//gcode缓冲区清空
void jmp_gcode_buff_clear(void)
{
	u32 i;
	for(i=0;i<GCODE_BUFF_SUM;i++)
	{
		jmp_gcode_buff[i]=0;
	}
	jmp_gcode_buff_sp=0;
	jmp_gcode_buff_ep=0;
	jmp_gcode_buff_full=0;
}

//行gcode缓冲区清空
void jmp_gcode_line_buff_clear(void)
{
	u32 i;
	for(i=0;i<GCODE_LINE_BUFF_SUM;i++)
	{
		jmp_gcode_line_buff[i]=0;
	}
	jmp_gcode_line_buff_sum=0;
}

//gcode指令参数缓冲区清空
void jmp_gcode_param_buff_clear(void)
{
	u32 i,j;
	for(i=0;i<GCODE_PARAM_SUM;i++)
	{
		for(j=0;j<GCODE_PARAM_LENGTH;j++)
		{
			jmp_gcode_param_buff[i][j]=0;
		}
	}
	jmp_gcode_param_buff_sum=0;
}

//获取gcode缓冲区中的总数
u32 jmp_gcode_buff_get_sum(void)
{
	u32 sum;
	if(jmp_gcode_buff_ep>jmp_gcode_buff_sp)
	{
		sum=jmp_gcode_buff_ep-jmp_gcode_buff_sp;
	}
	else if(jmp_gcode_buff_ep<jmp_gcode_buff_sp)
	{
		sum=GCODE_BUFF_SUM+jmp_gcode_buff_ep-jmp_gcode_buff_sp;
	}
	else
	{
		if(jmp_gcode_buff_full==1)
		{
			sum=GCODE_BUFF_SUM;
		}
		else
		{
			sum=0;
		}
	}
	return sum;
}

//获取gcode缓冲区中的剩余空间
u32 jmp_gcode_buff_get_remain(void)
{
	u32 remain;
	if(jmp_gcode_buff_ep>jmp_gcode_buff_sp)
	{
		remain=GCODE_BUFF_SUM-(jmp_gcode_buff_ep-jmp_gcode_buff_sp);
	}
	else if(jmp_gcode_buff_ep<jmp_gcode_buff_sp)
	{
		remain=GCODE_BUFF_SUM-(GCODE_BUFF_SUM+jmp_gcode_buff_ep-jmp_gcode_buff_sp);
	}
	else
	{
		if(jmp_gcode_buff_full==1)
		{
			remain=0;
		}
		else
		{
			remain=GCODE_BUFF_SUM;
		}
	}
	return remain;
}

//向gcode缓冲区压入一个字符
void jmp_gcode_buff_put(char data)
{
	jmp_gcode_buff[jmp_gcode_buff_ep]=data;
	jmp_gcode_buff_ep++;
	if(jmp_gcode_buff_ep==GCODE_BUFF_SUM)
	{
		jmp_gcode_buff_ep=0;
	}
	if(jmp_gcode_buff_ep==jmp_gcode_buff_sp)
	{
		jmp_gcode_buff_full=1;
	}
}

//从gcode缓冲区中取出一个字符
char jmp_gcode_buff_get(void)
{
	char data;
	data=jmp_gcode_buff[jmp_gcode_buff_sp];
	jmp_gcode_buff_sp++;
	if(jmp_gcode_buff_sp==GCODE_BUFF_SUM)
	{
		jmp_gcode_buff_sp=0;
	}
	if(jmp_gcode_buff_full==1)
	{
		jmp_gcode_buff_full=0;
	}
	return data;
}

//从gcode缓冲区中取出一行gcode
void jmp_gcode_get_line(void)
{
	char data;
	jmp_gcode_line_buff_clear();//清空行gcode缓冲区
	while(1)
	{
		u32 remain;
		if(jmp_config_state_struct.printing_abort==1)
		{
			//如果printing_abort为1，即执行了停止操作
			if(jmp_config_state_struct.reading_end==1)
			{
				//如果读取已经结束
				jmp_config_state_struct.printing_abort=0;
				jmp_config_state_struct.reading_end=0;
				jmp_gcode_buff_clear();
				jmp_config_state_struct.printing_run=0;
				xSemaphoreGive(JmpGUISem);//向GUI线程发送信号量和打印结束消息
				jmp_gui_send_message(GUI_PRINT_END_MESSAGE);
				break;
			}
		}
		if(jmp_config_state_struct.printing_hold==0)
		{
			//如果没有暂停
			remain=jmp_gcode_buff_get_sum();//获取gcode缓冲区中的字符数量
			if(remain>0)
			{
				//如果缓冲器中还有字符
				data=jmp_gcode_buff_get();//获取一个字符
				if(data=='\n')
				{
					//如果是换行符，代表一行结束
					break;
				}
				else
				{
					//如果不是换行符，代表一行没有结束，将字符放在行gcode缓冲区
					jmp_gcode_line_buff[jmp_gcode_line_buff_sum]=data;
					jmp_gcode_line_buff_sum++;
					if(jmp_gcode_line_buff_sum>=GCODE_LINE_BUFF_SUM)
					{
						break;
					}
				}
			}
			else
			{
				//如果缓冲区中没有了字符
				if(jmp_config_state_struct.reading_end==1)
				{
					//如果文件已经读完
					jmp_config_state_struct.reading_end=0;
					jmp_config_state_struct.printing_run=0;
					jmp_gui_print_end_time=jmp_print_run_time;
					xSemaphoreGive(JmpGUISem);//向GUI线程发送信号量和打印结束消息
					jmp_gui_send_message(GUI_PRINT_END_MESSAGE);
					break;
				}
				//等待缓冲区有数据
				vTaskDelay(1);
			}
		}
		else
		{
			//如果暂停了，等待，不获取gcode缓冲区中的字符
			vTaskDelay(500);
		}
	}
}

//G0指令获取参数
void jmp_gcode_g0_param(void)
{
	jmp_gcode_g1_param();
}
//G1指令获取参数
void jmp_gcode_g1_param(void)
{
	u32 i;
	char param;
	//分解指令参数
	for(i=1;i<jmp_gcode_param_buff_sum;i++)
	{
		param=jmp_gcode_param_buff[i][0];
		if(param=='X')
		{
			jmp_command_struct.param[0]=atof(&jmp_gcode_param_buff[i][1]);
			jmp_command_struct.param_flag|=(1<<0);
		}
		else if(param=='Y')
		{
			jmp_command_struct.param[1]=atof(&jmp_gcode_param_buff[i][1]);
			jmp_command_struct.param_flag|=(1<<1);
		}
		else if(param=='Z')
		{
			jmp_command_struct.param[2]=atof(&jmp_gcode_param_buff[i][1]);
			jmp_command_struct.param_flag|=(1<<2);
		}
		else if(param=='E')
		{
			jmp_command_struct.param[3]=atof(&jmp_gcode_param_buff[i][1]);
			jmp_command_struct.param_flag|=(1<<3);
		}
		else if(param=='F')
		{
			jmp_command_struct.param[4]=atof(&jmp_gcode_param_buff[i][1]);
			jmp_command_struct.param_flag|=(1<<4);
		}
		else
		{
		}
	}
}
//G4指令获取参数
void jmp_gcode_g4_param(void)
{
}
//G28指令获取参数
void jmp_gcode_g28_param(void)
{
}
//G90指令获取参数
void jmp_gcode_g90_param(void)
{
}
//G92指令获取参数
void jmp_gcode_g92_param(void)
{
	jmp_gcode_g1_param();
}
//G指令获取参数
void jmp_gcode_g_param(void)
{
	switch(jmp_command_struct.command)
	{
		case 0:
		{
			jmp_gcode_g0_param();
			break;
		}
		case 1:
		{
			jmp_gcode_g1_param();
			break;
		}
		case 4:
		{
			jmp_gcode_g4_param();
			break;
		}
		case 28:
		{
			jmp_gcode_g28_param();
			break;
		}
		case 90:
		{
			jmp_gcode_g90_param();
			break;
		}
		case 92:
		{
			jmp_gcode_g92_param();
			break;
		}
		default:
		{
			break;
		}
	}
}
//M32指令获取参数
void jmp_gcode_m32_param(void)
{
	jmp_gcode_g1_param();
}
//M82指令获取参数
void jmp_gcode_m82_param(void)
{
}
//M指令获取参数
void jmp_gcode_m_param(void)
{
	switch(jmp_command_struct.command)
	{
		case 32:
		{
			jmp_gcode_m32_param();
			break;
		}
		case 82:
		{
			jmp_gcode_m82_param();
			break;
		}
		default:
		{
			break;
		}
	}
}
//T指令获取参数
void jmp_gcode_t_param(void)
{
	switch(jmp_command_struct.command)
	{
		default:
		{
			break;
		}
	}
}
//S指令获取参数
void jmp_gcode_s_param(void)
{
	switch(jmp_command_struct.command)
	{
		default:
		{
			break;
		}
	}
}
//gcode解析函数
void jmp_gcode_analysis(void)
{
	u32 i;
	char command_type;//指令类型
	char command_num[4];//指令号存放数组
	char* token;
	jmp_gcode_get_line();//获取一行gcode
	jmp_gcode_param_buff_clear();//清空gcode指令参数缓冲区
	
	token=strtok(jmp_gcode_line_buff," ");//以空格为分割，拆分参数
	while(token!=NULL)
	{
		strcpy(&jmp_gcode_param_buff[jmp_gcode_param_buff_sum][0],token);
		jmp_gcode_param_buff_sum++;
		token=strtok(NULL," ");
	}
	
	for(i=0;i<4;i++)
	{
		command_num[i]=0;
	}
	command_type=jmp_gcode_param_buff[0][0];//获得指令类型
	strcpy(command_num,&jmp_gcode_param_buff[0][1]);//获得指令号
	
	jmp_command_struct_clear();//指令结构体清空
	
	if(command_type=='G')
	{
		jmp_command_struct.command_type=G_COMMAND;
	}
	else if(command_type=='M')
	{
		jmp_command_struct.command_type=M_COMMAND;
	}
	else if(command_type=='T')
	{
		jmp_command_struct.command_type=T_COMMAND;
	}else if(command_type=='S')
	{
		jmp_command_struct.command_type=S_COMMAND;
	}
	else
	{
	}
	
	jmp_command_struct.command=atoi(command_num);//通过转换获得指令号
	
	switch(jmp_command_struct.command_type)//处理指令
	{
		case G_COMMAND:
		{
			jmp_gcode_g_param();
			break;
		}
		case M_COMMAND:
		{
			jmp_gcode_m_param();
			break;
		}
		case T_COMMAND:
		{
			jmp_gcode_t_param();
			break;
		}
		case S_COMMAND:
		{
			jmp_gcode_s_param();
			break;
		}
		default:
		{
			break;
		}
	}
	
}
//gcode分析初始化
void jmp_gcode_analysis_init(void)
{
	
}




