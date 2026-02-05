#include "agent_loop.h"
#include "agent/context_builder.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "llm/llm_proxy.h"
#include "memory/session_mgr.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "agent";

static void agent_loop_task(void *arg)
{
    ESP_LOGI(TAG, "Agent loop started on core %d", xPortGetCoreID());

    /* Allocate large buffers from PSRAM */
    char *system_prompt = heap_caps_calloc(1, MIMI_CONTEXT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *messages_json = heap_caps_calloc(1, MIMI_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *history_json = heap_caps_calloc(1, MIMI_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *response_buf = heap_caps_calloc(1, MIMI_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);

    if (!system_prompt || !messages_json || !history_json || !response_buf) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffers");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        mimi_msg_t msg;
        esp_err_t err = message_bus_pop_inbound(&msg, UINT32_MAX);
        if (err != ESP_OK) continue;

        ESP_LOGI(TAG, "Processing message from %s:%s", msg.channel, msg.chat_id);

        /* 1. Build system prompt */
        context_build_system_prompt(system_prompt, MIMI_CONTEXT_BUF_SIZE);

        /* 2. Load session history */
        session_get_history_json(msg.chat_id, history_json,
                                 MIMI_LLM_STREAM_BUF_SIZE, MIMI_AGENT_MAX_HISTORY);

        /* 3. Build messages array (history + current message) */
        context_build_messages(history_json, msg.content,
                               messages_json, MIMI_LLM_STREAM_BUF_SIZE);

        /* 4. Call Claude API */
        err = llm_chat(system_prompt, messages_json, response_buf, MIMI_LLM_STREAM_BUF_SIZE);

        if (err == ESP_OK && response_buf[0]) {
            /* 5. Save to session */
            session_append(msg.chat_id, "user", msg.content);
            session_append(msg.chat_id, "assistant", response_buf);

            /* 6. Push response to outbound */
            mimi_msg_t out = {0};
            strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            out.content = strdup(response_buf);
            if (out.content) {
                message_bus_push_outbound(&out);
            }
        } else {
            /* Send error response */
            mimi_msg_t out = {0};
            strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            out.content = strdup(response_buf[0] ? response_buf : "Sorry, I encountered an error.");
            if (out.content) {
                message_bus_push_outbound(&out);
            }
        }

        /* Free inbound message content */
        free(msg.content);

        /* Log memory status */
        ESP_LOGI(TAG, "Free PSRAM: %d bytes",
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

esp_err_t agent_loop_init(void)
{
    ESP_LOGI(TAG, "Agent loop initialized");
    return ESP_OK;
}

esp_err_t agent_loop_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        agent_loop_task, "agent_loop",
        MIMI_AGENT_STACK, NULL,
        MIMI_AGENT_PRIO, NULL, MIMI_AGENT_CORE);

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
