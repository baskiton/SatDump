#include "scatterometer_handler.h"
#include "resources.h"
#include "common/projection/reprojector.h"
#include "core/style.h"
#include "common/map/map_drawer.h"
#include "common/widgets/stepped_slider.h"
#include "imgui/pfd/pfd_utils.h"
#include "main_ui.h"
#include "common/image/image_meta.h"

namespace satdump
{
    void ScatterometerViewerHandler::init()
    {
        products = (ScatterometerProducts *)ViewerHandler::products;

        if (products->get_scatterometer_type() == products->SCAT_TYPE_ASCAT)
            current_scat_type = SCAT_ASCAT;

        if (current_scat_type == SCAT_ASCAT)
        {
            select_channel_image_str += std::string("Channel 1 (Back Right)") + '\0';
            select_channel_image_str += std::string("Channel 2 (Down Left)") + '\0';
            select_channel_image_str += std::string("Channel 3 (Back Left)") + '\0';
            select_channel_image_str += std::string("Channel 4 (Front Right)") + '\0';
            select_channel_image_str += std::string("Channel 5 (Back Left)") + '\0';
            select_channel_image_str += std::string("Channel 6 (Front Left)") + '\0';

            ascat_select_channel_image_str += std::string("Back") + '\0';
            ascat_select_channel_image_str += std::string("Down") + '\0';
            ascat_select_channel_image_str += std::string("Front") + '\0';
        }
        else
        {
            for (int c = 0; c < (int)products->get_channel_cnt(); c++)
                select_channel_image_str += "Channel " + std::to_string(c + 1) + '\0';
        }

        try
        {
            overlay_handler.set_config(config::main_cfg["user"]["viewer_state"]["products_handler"][products->instrument_name]["overlay_cfg"], false);
        }
        catch (std::exception &)
        {
        }

        current_img.init_font(resources::getResourcePath("fonts/font.ttf"));
        update();
    }

    ScatterometerViewerHandler::~ScatterometerViewerHandler()
    {
        handler_thread_pool.stop();
        for (int i = 0; i < handler_thread_pool.size(); i++)
        {
            if (handler_thread_pool.get_thread(i).joinable())
                handler_thread_pool.get_thread(i).join();
        }

        config::main_cfg["user"]["viewer_state"]["products_handler"][products->instrument_name]["overlay_cfg"] = overlay_handler.get_config();
        config::saveUserConfig();
    }

    void ScatterometerViewerHandler::update()
    {
        if (selected_visualization_id == 0)
        {
            GrayScaleScatCfg cfg;
            cfg.channel = select_channel_image_id;
            cfg.min = scat_grayscale_min;
            cfg.max = scat_grayscale_max;
            current_img = make_scatterometer_grayscale(*products, cfg);
            image_view.update(current_img);
        }
        else if (selected_visualization_id == 1)
        {
            GrayScaleScatCfg cfg;
            if (current_scat_type == SCAT_ASCAT)
                cfg.channel = ascat_select_channel_id;
            else
                cfg.channel = select_channel_image_id;
            cfg.min = scat_grayscale_min;
            cfg.max = scat_grayscale_max;
            current_image_proj.clear();
            current_img = make_scatterometer_grayscale_projs(*products, cfg, nullptr, &current_image_proj);

            if (overlay_handler.enabled())
            {
                auto proj_func = satdump::reprojection::setupProjectionFunction(current_img.width(), current_img.height(), current_image_proj, {});
                overlay_handler.apply(current_img, proj_func);
            }

            image_view.update(current_img);
        }
    }

    void ScatterometerViewerHandler::asyncUpdate()
    {
        handler_thread_pool.clear_queue();
        handler_thread_pool.push([this](int)
                                 {   async_image_mutex.lock();
                            is_updating = true;
                            logger->info("Update image...");
                            update();
                            logger->info("Done");
                            is_updating = false;
                            async_image_mutex.unlock(); });
    }

    void ScatterometerViewerHandler::drawMenu()
    {
        if (ImGui::CollapsingHeader("Images"))
        {
            if (ImGui::RadioButton(u8"Raw Image", &selected_visualization_id, 0))
                asyncUpdate();
            if (ImGui::RadioButton(u8"Projected", &selected_visualization_id, 1))
                asyncUpdate();

            if (selected_visualization_id == 0 || selected_visualization_id == 1)
            {
                if (current_scat_type == SCAT_ASCAT && selected_visualization_id == 1)
                {
                    if (ImGui::Combo("###scatchannelcomboid", &ascat_select_channel_id, ascat_select_channel_image_str.c_str()))
                        asyncUpdate();
                }
                else
                {
                    if (ImGui::Combo("###scatchannelcomboid", &select_channel_image_id, select_channel_image_str.c_str()))
                        asyncUpdate();
                }

                ImGui::SetNextItemWidth(ImGui::GetWindowWidth() / 2);
                if (ImGui::SliderInt("##MinScat", &scat_grayscale_min, 0, 1e7, "Min: %d", ImGuiSliderFlags_AlwaysClamp))
                    asyncUpdate();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetWindowWidth() / 2);
                if (ImGui::SliderInt("##MaxScat", &scat_grayscale_max, 0, 1e7, "Max: %d", ImGuiSliderFlags_AlwaysClamp))
                    asyncUpdate();
            }

            bool save_disabled = is_updating;
            if (save_disabled)
                style::beginDisabled();
            if (ImGui::Button("Save"))
            {
                handler_thread_pool.push([this](int)
                                         {   async_image_mutex.lock();
                        is_updating = true;
                        logger->info("Saving Image...");
                        std::string default_path = config::main_cfg["satdump_directories"]["default_image_output_directory"]["value"].get<std::string>();
                        std::string saved_at = save_image_dialog(products->instrument_name + "_" +
                            ((selected_visualization_id == 1 && current_scat_type == SCAT_ASCAT) ? std::to_string(ascat_select_channel_id) : 
                            std::to_string(select_channel_image_id)), default_path, "Save Image", &current_img, &viewer_app->save_type);

                        if (saved_at == "")
                            logger->info("Save cancelled");
                        else
                            logger->info("Saved current image at %s", saved_at.c_str());
                        is_updating = false;
                        async_image_mutex.unlock(); });
            }
            if (save_disabled)
            {
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Updating, please wait...");
                style::endDisabled();
            }
        }

        if (ImGui::CollapsingHeader("Map Overlay"))
        {
            if (selected_visualization_id != 1)
                style::beginDisabled();

            if (overlay_handler.drawUI())
                asyncUpdate();

            if (selected_visualization_id != 1)
                style::endDisabled();
        }

        if (ImGui::CollapsingHeader("Projection"))
        {
            ImGui::BeginGroup();
            if (!canBeProjected())
                style::beginDisabled();
            if (ImGui::Button("Add to Projections"))
                addCurrentToProjections();
            ImGui::SameLine();
            proj_notif.draw();
            if (!canBeProjected())
                style::endDisabled();
            ImGui::EndGroup();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip();
                if (selected_visualization_id != 1)
                    ImGui::TextColored(style::theme.red, "Select projection view first!");
                ImGui::EndTooltip();
            }
        }
    }

    void ScatterometerViewerHandler::drawContents(ImVec2 win_size)
    {
        if (selected_visualization_id == 0 || selected_visualization_id == 1)
        {
            image_view.draw(win_size);
        }
    }

    float ScatterometerViewerHandler::drawTreeMenu()
    {
        return 0;
    }

    bool ScatterometerViewerHandler::canBeProjected()
    {
        return products->has_proj_cfg() &&
               products->has_tle() &&
               products->has_proj_cfg() &&
               products->has_timestamps &&
               selected_visualization_id == 1;
    }

    void ScatterometerViewerHandler::addCurrentToProjections()
    {
        if (canBeProjected())
        {
            try
            {
                nlohmann::json proj_cfg = current_image_proj;
                image::set_metadata_proj_cfg(current_img, proj_cfg);

                // Create projection title
                std::string timestring, object_name, instrument_name;
                if (products->has_timestamps)
                    timestring = "[" + timestamp_to_string(get_median(products->get_timestamps(select_channel_image_id))) + "] ";
                else
                    timestring = "";
                if (products->has_tle())
                    object_name = products->get_tle().name;
                else
                    object_name = "";
                if (timestring != "" || object_name != "")
                    object_name += "\n";
                if (instrument_cfg.contains("name"))
                    instrument_name = instrument_cfg["name"];
                else
                    instrument_name = products->instrument_name;

                viewer_app->projection_layers.push_back({ timestring + object_name + instrument_name, current_img });
            }
            catch (std::exception &e)
            {
                logger->error("Could not project image! %s", e.what());
            }

            proj_notif.set_message(style::theme.green, "Added!");
        }
        else
        {
            logger->error("Current image can't be projected!");
        }
    }
}
