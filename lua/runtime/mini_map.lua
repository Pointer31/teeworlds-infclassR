mini_map_layout = false
mini_map_right_border = 0
mini_map_max_players = 8
mini_map_timelimit = 90
mini_map_infection_delay = 5

---@param position Vec2
---@return boolean
function is_position_within_borders(position)
    if position.x > mini_map_right_border then
        return false
    end

    return true
end

function mini_layout_set_spawns()
    local human_spawns = Game.Controller:GetHumanSpawns()

    for i = 1, human_spawns:Size() do
        local position = human_spawns:At(i)
        Game.Controller:SetHumanSpawnEnabled(i, is_position_within_borders(position))
    end
end

function mini_layout_spawn_doors()
    local door = add_piercing_door_at(mini_map_right_border)
    door:SetOpen(false)
end

function mini_layout_validate()
    local players_number = Game.Controller:GetPlayersNumber()
    local total = players_number.Humans + players_number.Infected
    if total <= mini_map_max_players then
        return
    end

    Game.Context:SendChatTarget(-1,
        string.format("Too many players for this map layout (%d / %d).", total, mini_map_max_players))
    Game.Context:SendChatTarget(-1, "The doors have been removed from the map.")
    remove_all_doors()
    mini_map_layout = false
end

---@param player CIcPlayer
---@return Vec2
function Get_hero_flag_position(player)
    local flag_positions = Game.Controller:GetHeroFlagPositions()

    local suitable_positions = {}
    for i = 1, flag_positions:Size(), 1 do
        local position = flag_positions:At(i)
        if is_flag_position_suitable(position) then
            table.insert(suitable_positions, position)
        end
    end
    local count = table.getn(suitable_positions)
    if count == 0 then
        return nil
    end

    return suitable_positions[math.random(1, count)]
end

---@param position Vec2
---@return boolean
function is_flag_position_suitable(position)
    return is_position_within_borders(position)
end

function on_mini_map_tick()
    local tick = Game.Server.Tick
    if tick % 50 == 0 then
        mini_layout_validate()
    end
end

function on_world_reset()
    if mini_map_layout then
        mini_layout_spawn_doors()
    end
end

function on_round_started(round_type_str)
    mini_map_layout = false

    local players_number = Game.Controller:GetPlayersNumber()
    if players_number.Humans + players_number.Infected < mini_map_max_players then
        Game.Controller.TimeLimitSeconds = mini_map_timelimit
        Game.Controller.InfectionDelaySeconds = mini_map_infection_delay
        mini_layout_set_spawns()
        mini_map_layout = true
    end
end

function on_tick()
    if mini_map_layout then
        on_mini_map_tick()
    end
end

print("mini_map runtime loaded")
