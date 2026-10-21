// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/retro_widgets.hpp>
#include <ravenemudsk/ui/widgets.hpp>
#include <algorithm>
namespace ravenemudsk_ui {
void draw_leaderboards(ravenemudsk_ra::Client& client,const ravenemudsk_ra::Snapshot& game,const Fonts& fonts,bool tv){
    ImGui::PushID("retro-leaderboards");
    if(game.leaderboards.empty())ImGui::TextWrapped("Aucun classement disponible pour ce jeu.");
    const auto active=client.snapshot();
    const bool current=active.logged_in&&!game.game_hash.empty()&&game.game_hash==active.game_hash&&!active.cached_profile;
    if(!current&&!game.leaderboards.empty())ImGui::TextWrapped("Actualisez les succès de ce jeu pour consulter ses classements.");
    for(const auto& board:game.leaderboards){
        ImGui::PushID(static_cast<int>(board.id));
        ImGui::BeginDisabled(!current);
        // TV: the panel's large font, taller buttons and a readable description.
        const auto title=ellipsize(tv?ImGui::GetFont():fonts.body,board.title,std::max(30.f,ImGui::GetContentRegionAvail().x-64.f));
        if(button(title.c_str(),Icon::Trophy,ButtonStyle::Secondary,{ImGui::GetContentRegionAvail().x,tv?56.f:40.f}))client.fetch_ranking(board.id);
        ImGui::EndDisabled();
        if(tv){
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(color::text_muted));
            ImGui::TextWrapped("%s",board.description.c_str());ImGui::PopStyleColor();ImGui::Dummy({0.f,space::sm});
        }else{
            ImGui::PushFont(fonts.caption);ImGui::TextWrapped("%s",board.description.c_str());ImGui::PopFont();
        }
        ImGui::PopID();
    }
    const auto ranks=client.ranking();
    if(ranks.id && std::any_of(game.leaderboards.begin(),game.leaderboards.end(),[&](const auto& b){return b.id==ranks.id;})){
        ImGui::Separator();
        ImGui::BeginDisabled(ranks.loading||!current);
        if(button(ranks.around_user?"Voir le top 20":"Autour de moi",Icon::Recent,ButtonStyle::Ghost))
            client.fetch_ranking(ranks.id,!ranks.around_user);
        ImGui::EndDisabled();
        if(ranks.loading)ImGui::TextDisabled("Chargement du classement…");
        if(!ranks.error.empty())ImGui::TextWrapped("%s",ranks.error.c_str());
        if(!ranks.loading&&ranks.error.empty()&&ranks.entries.empty())ImGui::TextWrapped("Aucun score publié pour ce classement.");
        if(!ranks.entries.empty()&&ImGui::BeginTable("##ranking",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Rang",ImGuiTableColumnFlags_WidthFixed,tv?80.f:52.f);
            ImGui::TableSetupColumn("Joueur",ImGuiTableColumnFlags_WidthStretch,2.f);
            ImGui::TableSetupColumn("Score",ImGuiTableColumnFlags_WidthStretch,1.f);
            ImGui::TableHeadersRow();
            for(const auto& e:ranks.entries){
                ImGui::TableNextRow();
                if(e.current_user)ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(color::gold));
                ImGui::TableNextColumn();ImGui::Text("%u",e.rank);
                ImGui::TableNextColumn();ImGui::TextWrapped("%s%s",e.username.c_str(),e.current_user?" (vous)":"");
                ImGui::TableNextColumn();ImGui::TextUnformatted(e.score.c_str());
                if(e.current_user)ImGui::PopStyleColor();
            }
            ImGui::EndTable();
            ImGui::TextDisabled("%u participants",ranks.total);
        }
    }
    ImGui::PopID();
}
}
