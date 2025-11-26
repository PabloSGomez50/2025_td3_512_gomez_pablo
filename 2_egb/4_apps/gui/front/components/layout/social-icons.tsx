import { Facebook, Twitter, Instagram, Linkedin } from "lucide-react"
import Link from "next/link"

const facebookUrl = process.env.NEXT_PUBLIC_FACEBOOK_URL || '#'
const twitterUrl = process.env.NEXT_PUBLIC_TWITTER_URL || '#'
const instagramUrl = process.env.NEXT_PUBLIC_INSTAGRAM_URL || '#'
const linkedinUrl = process.env.NEXT_PUBLIC_LINKEDIN_URL || '#'


export default function SocialIcons({
    className,
    textClassName = "text-gray-500 hover:text-teal-600",
}: {
    className?: string,
    textClassName?: string,
}) {
    return (
        <div className={`flex items-center space-x-2 ${className || ""}`}>
            <Link href={facebookUrl} className={textClassName} target="_blank" rel="noopener noreferrer">
                <Facebook size={20} />
                <span className="sr-only">Facebook</span>
            </Link>
            <Link href={twitterUrl} className={textClassName} target="_blank" rel="noopener noreferrer">
                <Twitter size={20} />
                <span className="sr-only">Twitter</span>
            </Link>
            <Link href={instagramUrl} className={textClassName} target="_blank" rel="noopener noreferrer">
                <Instagram size={20} />
                <span className="sr-only">Instagram</span>
            </Link>
            <Link href={linkedinUrl} className={textClassName} target="_blank" rel="noopener noreferrer">
                <Linkedin size={20} />
                <span className="sr-only">LinkedIn</span>
            </Link>
        </div>
    );
}