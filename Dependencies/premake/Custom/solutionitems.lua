require('vstudio')

local p = premake
local m = p.vstudio.sln2005

p.api.register {
	name = "solutionitems",
	scope = "workspace",
	kind = "list:string"
}

p.override(m, "projects", function(base, wks)
	if wks.solutionitems and #wks.solutionitems > 0 then
		local solutionFolderGUID = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}" -- See https://www.codeproject.com/Reference/720512/List-of-Visual-Studio-Project-Type-GUIDs
		p.push("Project(\"" .. solutionFolderGUID .. "\") = \"Solution Items\", \"Solution Items\", \"{" .. os.uuid("Solution Items:" .. wks.name) .. "}\"")
		p.push("ProjectSection(SolutionItems) = preProject")

		for _, path in ipairs(wks.solutionitems) do
			p.w(path .. " = " .. path)
		end

		p.pop("EndProjectSection")
		p.pop("EndProject")
	end
	base(wks)
end)
