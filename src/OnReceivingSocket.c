/*
 * hetao - High Performance Web Server
 * author	: calvin
 * email	: calvinwilliams@163.com
 *
 * Licensed under the LGPL v2.1, see the file LICENSE in base directory.
 */

#include "hetao_in.h"

int OnReceivingSocket( struct HetaoEnv *p_env , struct HttpSession *p_http_session )
{
	struct epoll_event	event ;
	
	int			nret = 0 ;
	
	/* ��һ��HTTP���� */
	nret = ReceiveHttpRequestNonblock( p_http_session->netaddr.sock , p_http_session->ssl , p_http_session->http ) ;
	if( nret == FASTERHTTP_INFO_NEED_MORE_HTTP_BUFFER )
	{
		/* û������ */
		DebugLog( __FILE__ , __LINE__ , "ReceiveHttpRequestNonblock return FASTERHTTP_INFO_NEED_MORE_HTTP_BUFFER" );
	}
	else if( nret )
	{
		/* ���ձ����� */
		if( nret == FASTERHTTP_ERROR_TCP_CLOSE )
		{
			ErrorLog( __FILE__ , __LINE__ , "http socket closed detected" );
			return 1;
		}
		else if( nret == FASTERHTTP_INFO_TCP_CLOSE )
		{
			InfoLog( __FILE__ , __LINE__ , "http socket closed detected" );
			return 1;
		}
		else
		{
			ErrorLog( __FILE__ , __LINE__ , "ReceiveHttpRequestNonblock failed[%d] , errno[%d]" , nret , errno );
			return 1;
		}
	}
	else
	{
		/* ����һ��HTTP���� */
		char			*host = NULL ;
		int			host_len ;
		
		struct HttpBuffer	*b = NULL ;
		
		DebugLog( __FILE__ , __LINE__ , "ReceiveHttpRequestNonblock done" );
		
		UpdateHttpSessionTimeoutTreeNode( p_env , p_http_session , GETSECONDSTAMP + p_env->p_config->http_options.timeout );
		
		b = GetHttpRequestBuffer(p_http_session->http) ;
		DebugHexLog( __FILE__ , __LINE__ , GetHttpBufferBase(b,NULL) , GetHttpBufferLength(b) , "HttpRequestBuffer" );
		
		/* ��ѯ�������� */
		host = QueryHttpHeaderPtr( p_http_session->http , "Host" , & host_len ) ;
		if( host == NULL )
			host = "" , host_len = 0 ;
		p_http_session->p_virtualhost = QueryVirtualHostHashNode( p_http_session->p_listen_session , host , host_len ) ;
		if( p_http_session->p_virtualhost == NULL && p_http_session->p_listen_session->p_virtualhost_default )
		{
			p_http_session->p_virtualhost = p_http_session->p_listen_session->p_virtualhost_default ;
		}
		
		/* �ֽ�URI */
		memset( & (p_http_session->http_uri) , 0x00 , sizeof(struct HttpUri) );
		nret = SplitHttpUri( p_http_session->p_virtualhost->wwwroot , GetHttpHeaderPtr_URI(p_http_session->http,NULL) , GetHttpHeaderLen_URI(p_http_session->http) , & (p_http_session->http_uri) ) ;
		if( nret )
		{
			ErrorLog( __FILE__ , __LINE__ , "SplitHttpUri failed[%d] , errno[%d]" , nret , errno );
			return HTTP_BAD_REQUEST;
		}
		
		/* ����HTTP���� */
		if( p_http_session->p_virtualhost )
		{
			DebugLog( __FILE__ , __LINE__ , "QueryVirtualHostHashNode[%.*s] ok , wwwroot[%s]" , host_len , host , p_http_session->p_virtualhost->wwwroot );
			
			if( p_http_session->p_virtualhost->forward_rule[0] == 0
				|| p_http_session->p_listen_session->virtualhost_count <= 0
				|| p_http_session->http_uri.ext_filename_len != p_http_session->p_virtualhost->forward_type_len
				|| MEMCMP( p_http_session->http_uri.ext_filename_base , != , p_http_session->p_virtualhost->forward_type , p_http_session->http_uri.ext_filename_len ) )
			{
				/* �ȸ�ʽ����Ӧͷ���У��óɹ�״̬�� */
				nret = FormatHttpResponseStartLine( HTTP_OK , p_http_session->http , 0 ) ;
				if( nret )
				{
					ErrorLog( __FILE__ , __LINE__ , "FormatHttpResponseStartLine failed[%d] , errno[%d]" , nret , errno );
					return 1;
				}
				
				/* ����HTTP���� */
				nret = ProcessHttpRequest( p_env , p_http_session , p_http_session->p_virtualhost->wwwroot , GetHttpHeaderPtr_URI(p_http_session->http,NULL) , GetHttpHeaderLen_URI(p_http_session->http) ) ;
				if( nret != HTTP_OK )
				{
					/* ��ʽ����Ӧͷ���壬�ó���״̬�� */
					nret = FormatHttpResponseStartLine( nret , p_http_session->http , 1 ) ;
					if( nret )
					{
						ErrorLog( __FILE__ , __LINE__ , "FormatHttpResponseStartLine failed[%d] , errno[%d]" , nret , errno );
						return 1;
					}
				}
				else
				{
					DebugLog( __FILE__ , __LINE__ , "ProcessHttpRequest ok" );
				}
			}
			else
			{
				/* ѡ��ת������� */
				nret = SelectForwardAddress( p_env , p_http_session ) ;
				if( nret == HTTP_OK )
				{
					/* ����ת������� */
					nret = ConnectForwardServer( p_env , p_http_session ) ;
					if( nret == HTTP_OK )
					{
						/* �ݽ�ԭ�����¼� */
						memset( & event , 0x00 , sizeof(struct epoll_event) );
						event.events = EPOLLRDHUP | EPOLLERR ;
						event.data.ptr = p_http_session ;
						nret = epoll_ctl( p_env->p_this_process_info->epoll_fd , EPOLL_CTL_MOD , p_http_session->netaddr.sock , & event ) ;
						if( nret == -1 )
						{
							ErrorLog( __FILE__ , __LINE__ , "epoll_ctl failed , errno[%d]" , errno );
							return -1;
						}
						
						return 0;
					}
					else
					{
						ErrorLog( __FILE__ , __LINE__ , "SelectForwardAddress failed[%d] , errno[%d]" , nret , errno );
						
						/* ��ʽ����Ӧͷ���壬�ó���״̬�� */
						nret = FormatHttpResponseStartLine( nret , p_http_session->http , 1 ) ;
						if( nret )
						{
							ErrorLog( __FILE__ , __LINE__ , "FormatHttpResponseStartLine failed[%d] , errno[%d]" , nret , errno );
							return 1;
						}
					}
				}
				else
				{
					ErrorLog( __FILE__ , __LINE__ , "SelectForwardAddress failed[%d] , errno[%d]" , nret , errno );
					
					/* ��ʽ����Ӧͷ���壬�ó���״̬�� */
					nret = FormatHttpResponseStartLine( nret , p_http_session->http , 1 ) ;
					if( nret )
					{
						ErrorLog( __FILE__ , __LINE__ , "FormatHttpResponseStartLine failed[%d] , errno[%d]" , nret , errno );
						return 1;
					}
				}
			}
		}
		else
		{
			DebugLog( __FILE__ , __LINE__ , "QueryVirtualHostHashNode[%.*s] not found" , host_len , host );
			
			/* ��ʽ����Ӧͷ���壬�ó���״̬�� */
			nret = FormatHttpResponseStartLine( HTTP_FORBIDDEN , p_http_session->http , 1 ) ;
			if( nret )
			{
				ErrorLog( __FILE__ , __LINE__ , "FormatHttpResponseStartLine failed[%d] , errno[%d]" , nret , errno );
				return 1;
			}
		}
		
		b = GetHttpResponseBuffer(p_http_session->http) ;
		DebugHexLog( __FILE__ , __LINE__ , GetHttpBufferBase(b,NULL) , GetHttpBufferLength(b) , "HttpResponseBuffer" );
		
		/* ע��epollд�¼� */
		memset( & event , 0x00 , sizeof(struct epoll_event) );
		event.events = EPOLLOUT | EPOLLERR ;
		event.data.ptr = p_http_session ;
		nret = epoll_ctl( p_env->p_this_process_info->epoll_fd , EPOLL_CTL_MOD , p_http_session->netaddr.sock , & event ) ;
		if( nret == -1 )
		{
			ErrorLog( __FILE__ , __LINE__ , "epoll_ctl failed , errno[%d]" , errno );
			return -1;
		}
		
		/* ֱ����һ�� */
		/*
		if( p_env->p_config->worker_processes == 1 )
		{
			nret = OnSendingSocket( p_env , p_http_session ) ;
			if( nret > 0 )
			{
				DebugLog( __FILE__ , __LINE__ , "OnSendingSocket done[%d]" , nret );
				return nret;
			}
			else if( nret < 0 )
			{
				ErrorLog( __FILE__ , __LINE__ , "OnSendingSocket failed[%d] , errno[%d]" , nret , errno );
				return nret;
			}
			else
			{
				DebugLog( __FILE__ , __LINE__ , "OnSendingSocket ok" );
			}
		}
		*/
	}
	
	return 0;
}
